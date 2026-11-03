#!/usr/bin/env python3
"""
tools/new_project.py: export the base into a scratch folder and check that

  1. bad arguments are refused before anything is written
  2. the export has the sources, third_party/, BASE_VERSION, and no web site,
     legacy/ or repository plumbing
  3. --board / --mb-unit / --mb-baud / --mb-format land in port_cfg.h / app.h
  4. the exported project builds on its own: CMake configure + test_sys run
     inside it (the settings service those defaults feed)
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tools", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--cmake", default="cmake")
    a = ap.parse_args()

    tool = os.path.join(a.tools, "new_project.py")
    fails = []

    def check(cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            fails.append(what)

    def run(*args):
        return subprocess.run([sys.executable, tool] + list(args), capture_output=True, text=True)

    root = tempfile.mkdtemp(prefix="new_project_", dir=a.workdir)
    try:
        dest = os.path.join(root, "meter")

        for bad, what in ((["--mb-unit", "0"], "--mb-unit 0"), (["--mb-unit", "248"], "--mb-unit 248"),
                          (["--mb-baud", "1234"], "--mb-baud 1234"), (["--mb-format", "7N1"], "--mb-format 7N1"),
                          (["--board", "x" * 16], "--board of 16 chars")):
            r = run(dest, *bad)
            check(r.returncode != 0 and not os.path.exists(dest), "refused: " + what)

        r = run(dest, "--board", "meter-v1", "--mb-unit", "5", "--mb-baud", "19200", "--mb-format", "8e1")
        check(r.returncode == 0, "export ok" + ("" if r.returncode == 0 else ": " + r.stderr.strip()))

        def has(*p):
            return os.path.exists(os.path.join(dest, *p))

        check(has("src", "kernel", "src", "task.c") and has("src", "services", "sys", "settings.c"), "sources")
        check(has("third_party", "nanomodbus", "nanomodbus.c") and has("third_party", "stm32l1"), "third_party/")
        check(has("CMakeLists.txt") and has("platformio.ini") and has("tools", "stack_report.py"), "build files, tools/")
        check(not has("legacy") and not has(".github") and not has("docs", "play") and not has("docs", "index.html"),
              "no legacy/, .github/, web site")

        app_h = open(os.path.join(dest, "src", "app", "app.h"), encoding="utf-8").read()
        port_h = open(os.path.join(dest, "src", "port", "stm32l151", "port_cfg.h"), encoding="utf-8").read()
        base_v = open(os.path.join(dest, "BASE_VERSION"), encoding="utf-8").read()
        check("#define APP_MB_UNIT_ID\t\t\t\t\t(5)" in app_h, "app.h: APP_MB_UNIT_ID 5")
        check("#define APP_MB_BAUD\t\t\t\t\t\t(19200)" in app_h, "app.h: APP_MB_BAUD 19200")
        check("#define APP_MB_FORMAT\t\t\t(1)" in app_h, "app.h: APP_MB_FORMAT 1 (8E1)")
        check('"meter-v1"' in port_h, "port_cfg.h: board name")
        check("modbus defaults: unit 5, 19200 baud, 8E1" in base_v and "board name: meter-v1" in base_v,
              "BASE_VERSION records them")

        r = run(dest)
        check(r.returncode != 0, "refused: destination not empty")

        # the export stands alone: configure it and run one host test inside it
        build = os.path.join(root, "build")
        ok = subprocess.run([a.cmake, "-S", dest, "-B", build], capture_output=True, text=True).returncode == 0
        ok = ok and subprocess.run([a.cmake, "--build", build, "-j", "4", "--target", "test_sys"],
                                   capture_output=True, text=True).returncode == 0
        ok = ok and subprocess.run([os.path.join(build, "test_sys")], capture_output=True, text=True).returncode == 0
        check(ok, "exported project: cmake + test_sys pass")
    finally:
        shutil.rmtree(root, ignore_errors=True)

    print("%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
