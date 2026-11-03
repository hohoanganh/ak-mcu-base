#!/usr/bin/env python3
"""
End-to-end test of the product settings (services/sys/settings, app/app_cfg.c)
on ak_sim: the shell 'cfg' command, a value kept across resets in the
simulated EEPROM, the RS485 port taking it only after a reset, bad values
refused, 'cfg reset' back to the build defaults.

  1. blank NVM: build defaults (unit 1, 9600 8N1, timeout 200 ms)
  2. out-of-range values refused, nothing changes
  3. unit 7, 19200 8E1, timeout 300: 'cfg' shows them unsaved, the port
     ('mb') still runs the old ones
  4. 'cfg save', restart: loaded from NVM, the port runs them
  5. 'cfg reset' + 'cfg save', restart: defaults again
"""
import argparse
import os
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("--tools", required=True)
    ap.add_argument("--workdir", required=True)
    a = ap.parse_args()

    sys.path.insert(0, a.tools)
    import ak_fw
    import mkimage

    flash = os.path.join(a.workdir, "sim_cfg_flash.bin")
    img = os.path.join(a.workdir, "sim_cfg.img")
    mkimage.main(["synthetic", "-o", img, "--version", "1.0.0", "--size", "8000", "--seed", "5"])

    fails = []

    def check(cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            fails.append(what)

    def wait_app(fw, timeout=20.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                if fw.info(timeout=0.3, retries=1)["role"] == "app":
                    return True
            except ak_fw.FwError:
                pass
        return False

    def sh(fw, cmd, expect):
        fw.clear_text()
        fw.send_text(cmd + "\r")
        check(fw.wait_text(expect), "%-22s -> %s" % (cmd, expect))

    def start(fresh=False):
        return ak_fw.AkFw(ak_fw.SimLink([a.sim, "--flash", flash] + (["--fresh"] if fresh else [])))

    # an app image to run: the simulated board starts blank
    fw = start(fresh=True)
    try:
        fw.wait_ready(5)
        fw.upload(open(img, "rb").read())
        fw.install()
        check(wait_app(fw), "app installed and running")
        fw.confirm()

        sh(fw, "cfg", "unit 1, baud 9600 8N1, timeout 200 ms (defaults)")
        sh(fw, "cfg unit 300", "unit 1..247")
        sh(fw, "cfg baud 1234", "baud: 1200")
        sh(fw, "cfg format 7N1", "format: 8N1 8E1 8O1 8N2")
        sh(fw, "cfg timeout 5", "timeout 20..5000 ms")
        sh(fw, "cfg", "unit 1, baud 9600 8N1, timeout 200 ms (defaults)")

        sh(fw, "cfg unit 7", "not saved yet")
        sh(fw, "cfg baud 19200", "not saved yet")
        sh(fw, "cfg format 8E1", "not saved yet")
        sh(fw, "cfg timeout 300", "unit 7, baud 19200 8E1, timeout 300 ms (not saved yet")
        sh(fw, "cfg", "(defaults, changed: 'cfg save')")
        sh(fw, "mb", "slave unit 1, 9600 8N1")
        sh(fw, "cfg save", "saved, takes effect after 'reboot'")
    finally:
        fw.close()

    fw = start()
    try:
        check(wait_app(fw), "restart -> app")
        sh(fw, "cfg", "unit 7, baud 19200 8E1, timeout 300 ms (saved)")
        sh(fw, "mb", "slave unit 7, 19200 8E1")
        sh(fw, "cfg reset", "defaults: unit 1, baud 9600 8N1")
        sh(fw, "cfg save", "saved")
    finally:
        fw.close()

    fw = start()
    try:
        check(wait_app(fw), "restart -> app")
        sh(fw, "cfg", "unit 1, baud 9600 8N1, timeout 200 ms (saved)")
    finally:
        fw.close()

    print("%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
