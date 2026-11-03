#!/usr/bin/env python3
"""
stack_report.py - worst-case stack of a firmware image, worked out at build time.

GCC writes, for the final (LTO) code, the stack frame of every function and
every call it makes (-fcallgraph-info=su, see CMake option AK_STACK_REPORT).
This script walks that graph:

  main stack   deepest path from reset_handler (main loop, every handler)
  interrupts   deepest path of each *_handler in the vector table, plus the
               32 B the core pushes on entry; counted as if all of them nest
               at once (an upper bound: the port uses fewer priority levels)

and compares the sum with the RAM left for the stack: from _heap_start (end
of .bss) to _estack, read from the .elf.

Calls through a function pointer (task handlers, shell commands, screens,
the console output hook...) show up as one "indirect call" node, with the
source line of the call. POINTER_TARGETS below says, per source file, which
functions such a call can reach (by name). A call site in a file without a
rule is bounded by every function whose address is taken (nothing calls it
directly): an upper bound that may be far too high, so the report names the
site - add a rule for it. A cycle (recursion) cannot be bounded and fails.

  python tools/stack_report.py build/stm32l151/app.elf build/stm32l151/app.elf.ltrans*.ci
  python tools/stack_report.py --min-margin 2048 ...    exit 1 if the margin is smaller
"""

import argparse
import glob
import re
import subprocess
import sys

NODE = re.compile(r'^node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'^edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" label: "([^"]*)"')
SIZE = re.compile(r"(\d+) bytes \(([a-z, ]+)\)")
INDIRECT = "__indirect_call"
EXC_FRAME = 32          # r0-r3, r12, lr, pc, xPSR pushed by the core

# source file of a call through a pointer -> names it can reach (regex on the
# function name). Keep in step with the code: a new pointer call in a file
# not listed here shows up as "no rule" in the report.
POINTER_TARGETS = {
    "src/kernel/src/task.c":            r"^task_",                      # task and polling tables
    "src/app/task_console.c":           r"^cmd_",                       # shell_cmds[] (and 'crash test fault': no target)
    "src/common/xprintf.c":             r"_xputc$|^console_tx$",        # xfunc_out
    "src/services/fw/fw_proto.c":       r"^console_tx$|_proto_ext$",    # fw_proto_cfg_t tx / ext
    "src/services/modbus/mb_slave.c":   r"^on_write$",                  # mb_slave_map_t.on_write
    "src/services/modbus/mb_ota.c":     r"^on_ota_commit$",
    "src/services/modbus/mb_master.c":  r"done$",                       # mb_master_send() callback
    "src/services/sys/flog.c":          r"^kit_store_(read|write|erase)$",  # flog_ops_t (weather log)
    "src/demo/task_ui.c":               r"^(?!handle_)[a-z0-9]+_(enter|key|frame|leave)$",  # ui_screen_t of every screen
    "src/demo/blk_vm.c":                r"^io_|^ui_rand$",              # blk_io_t of scr_blocks.c
    "src/demo/video.c":                 r"^kit_store_read$",            # video_read_t
    "src/demo/ui_common.c":             r"^hal_console_putc$",          # screen dump / stream output
    "third_party/nanomodbus/nanomodbus.c":
        r"^port_read$|^port_write$|^port_send$|^tcp_read$|^tcp_write$|^flush$|crc_calc$|^cb_",
}


def short(title):
    return title.rsplit(":", 1)[-1] if ".o:" in title else title


def load(files):
    size, kind, where, calls = {}, {}, {}, {}
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = NODE.match(line)
                if m:
                    title, label = m.groups()
                    if title == INDIRECT:
                        continue
                    parts = label.split("\\n")
                    s = SIZE.search(label)
                    size[title] = int(s.group(1)) if s else 0
                    kind[title] = s.group(2) if s else "unknown (library)"
                    where[title] = parts[1] if len(parts) > 1 else ""
                    calls.setdefault(title, [])
                    continue
                m = EDGE.match(line)
                if m:
                    src, dst, site = m.groups()
                    if dst == INDIRECT:
                        dst = INDIRECT + "@" + site.rsplit(":", 2)[0]
                    calls.setdefault(src, []).append(dst)
    # callees named by a bare symbol that only exists with a file prefix elsewhere
    by_name = {}
    for t in size:
        by_name.setdefault(short(t), []).append(t)
    for t, cs in calls.items():
        calls[t] = [c if c in size or c.startswith(INDIRECT) or c not in by_name else by_name[c][0] for c in cs]
    return size, kind, where, calls


def symbol(elf, name, nm):
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("ci", nargs="+", help=".ci files GCC wrote at link time")
    ap.add_argument("--glob", action="store_true", help="the ci arguments are patterns (CMake passes them unexpanded)")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--min-margin", type=int, default=None, help="fail when fewer bytes are left")
    ap.add_argument("--top", type=int, default=8, help="indirect targets to list")
    a = ap.parse_args()

    files = sorted(f for p in a.ci for f in glob.glob(p)) if a.glob else a.ci
    if not files:
        sys.exit("no .ci file: link with -fcallgraph-info=su and LTO")
    size, kind, where, calls = load(files)
    called = {c for cs in calls.values() for c in cs}
    isrs = sorted(t for t in size if short(t).endswith("_handler")
                  and short(t) not in ("reset_handler", "default_handler"))
    roots = [t for t in size if short(t) == "reset_handler"]
    if not roots:
        sys.exit("no reset_handler in the call graph: wrong .ci files?")
    # address-taken functions: the bound for a pointer call without a rule
    taken = [t for t in size if t not in called and t not in isrs and t not in roots]

    no_rule = set()

    def pointer_targets(src, site_file):
        rel = re.sub(r"^.*?/(src|third_party)/", r"\1/", site_file)
        rule = POINTER_TARGETS.get(rel)
        if rule is None:
            no_rule.add(rel)
            pool = taken
        else:
            pool = [t for t in size if re.search(rule, short(t))]
        return [t for t in pool if t != src]

    memo, path_of = {}, {}
    errors = []
    cycles = set()

    def worst(t, stack=()):
        if t in memo:
            return memo[t]
        if t in stack:
            loop = [short(x) for x in stack[stack.index(t):]]
            k = loop.index(min(loop))
            cycles.add(tuple(loop[k:] + loop[:k]))
            return 0
        if t not in size:           # library function without a frame size (newlib)
            return 0
        best, via = 0, None
        for c in calls.get(t, []):
            if c.startswith(INDIRECT):
                for p in pointer_targets(t, c.split("@", 1)[1]):
                    w = worst(p, stack + (t,))
                    if w > best:
                        best, via = w, p
                continue
            w = worst(c, stack + (t,))
            if w > best:
                best, via = w, c
        memo[t] = size[t] + best
        path_of[t] = via
        return memo[t]

    def show_path(t):
        out = []
        seen = set()
        while t is not None and t not in seen:
            seen.add(t)
            out.append("%s %d" % (short(t), size.get(t, 0)))
            t = path_of.get(t)
        return " > ".join(out)

    main_worst = worst(roots[0])
    print("main stack (reset_handler): %d B" % main_worst)
    print("  " + show_path(roots[0]))
    print("deepest functions reached through a pointer:")
    for t in sorted((t for t in taken if worst(t)), key=worst, reverse=True)[:a.top]:
        print("  %5d  %s" % (worst(t), short(t)))
    irq_total = 0
    print("interrupts (+%d B frame each, all nested):" % EXC_FRAME)
    for t in isrs:
        w = worst(t) + EXC_FRAME
        irq_total += w
        print("  %5d  %s" % (w, short(t)))
    total = main_worst + irq_total
    print("worst case: %d B" % total)

    unknown = sorted({short(c) for cs in calls.values() for c in cs if c not in size and not c.startswith(INDIRECT)})
    if unknown:
        print("counted as 0 B (library, no frame size): " + ", ".join(unknown))
    dyn = [short(t) for t in size if "dynamic" in kind[t] and "bounded" not in kind[t]]
    if dyn:
        errors.append("unbounded dynamic stack (alloca / VLA): " + ", ".join(dyn))

    for c in sorted(cycles)[:10]:
        errors.append("recursion: " + " -> ".join(c + (c[0],)))
    if len(cycles) > 10:
        errors.append("... and %d more cycles" % (len(cycles) - 10))
    if no_rule:
        print("WARNING: pointer calls without a rule (bounded by every address-taken function): "
              + ", ".join(sorted(no_rule)) + " - add them to POINTER_TARGETS")

    rc = 0
    heap = symbol(a.elf, "_heap_start", a.nm)
    top = symbol(a.elf, "_estack", a.nm)
    if heap is not None and top is not None:
        avail = top - heap
        margin = avail - total
        print("RAM for the stack: %d B (_heap_start 0x%08X .. _estack 0x%08X), margin %d B" % (avail, heap, top, margin))
        if a.min_margin is not None and margin < a.min_margin:
            print("FAIL: margin %d B < %d B" % (margin, a.min_margin))
            rc = 1
    for e in errors:
        print("ERROR: " + e)
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
