#!/usr/bin/env python3
"""
End-to-end test on ak_sim (simulated bootloader + app) using the real
tools/ak_fw.py and tools/mkimage.py, as when flashing a board:

  1. blank flash -> bootloader in loader mode
  2. upload v1.0.0 via loader -> boot installs -> app v1.0.0 runs
  3. OTA v2.1.0 via app -> reset -> boot installs -> app v2.1.0
  4. wrong-board image rejected, app keeps running
  5. shell 'ver' reports the right version
  6. trial + rollback: v2 is on trial, CONFIRM ends it; v3 installed and
     confirmed, ROLLBACK brings v2 back; v3 installed again and never
     confirmed: three resets later the bootloader rolls back to v2
  7. restart the process: still v2.1.0 (flash persisted)
  8. LOADER from app -> bootloader; RUN -> back to app
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

    flash = os.path.join(a.workdir, "sim_ota_flash.bin")
    img1 = os.path.join(a.workdir, "sim_v1.img")
    img2 = os.path.join(a.workdir, "sim_v2.img")
    img3 = os.path.join(a.workdir, "sim_v3.img")
    img_bad = os.path.join(a.workdir, "sim_bad.img")
    mkimage.main(["synthetic", "-o", img1, "--version", "1.0.0", "--size", "9000", "--seed", "1"])
    mkimage.main(["synthetic", "-o", img2, "--version", "2.1.0", "--size", "20000", "--seed", "2"])
    mkimage.main(["synthetic", "-o", img3, "--version", "3.0.0", "--size", "12000", "--seed", "3"])
    mkimage.main(["synthetic", "-o", img_bad, "--version", "9.9.9", "--board", "other", "--seed", "3"])

    fails = []

    def check(cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            fails.append(what)

    def wait_role(fw, role, ver=None, timeout=20.0):
        import time
        end = time.time() + timeout
        while time.time() < end:
            try:
                i = fw.info(timeout=0.3, retries=1)
                if i["role"] == role and (ver is None or i["version"] == ver):
                    return i
            except ak_fw.FwError:
                pass
        return None

    # --- 1..5 ---
    fw = ak_fw.AkFw(ak_fw.SimLink([a.sim, "--flash", flash, "--fresh"]))
    try:
        i = fw.wait_ready(5)
        check(i["role"] == "bootloader", "blank flash -> loader (%s)" % i["role"])
        check(i["board"] == "ak-host", "board name")

        ver, _ = fw.upload(open(img1, "rb").read())
        check(ver == "1.0.0", "upload v1 via loader verified")
        fw.install()
        check(wait_role(fw, "app", "1.0.0") is not None, "boot installed v1, app running")

        ver, size = fw.upload(open(img2, "rb").read())
        check(ver == "2.1.0" and size == 20000, "OTA upload v2 via app verified")
        fw.install()
        check(wait_role(fw, "app", "2.1.0") is not None, "boot installed v2, app running")

        try:
            fw.upload(open(img_bad, "rb").read())
            check(False, "wrong board image rejected")
        except ak_fw.FwError as e:
            check("wrong board" in str(e), "wrong board image rejected (%s)" % e)
        check(wait_role(fw, "app", "2.1.0") is not None, "app still v2 after rejected image")

        fw.clear_text()
        fw.send_text("ver\r")
        check(fw.wait_text("app v2.1.0"), "shell 'ver' -> v2.1.0")
        fw.send_text("info\r")
        check(fw.wait_text("installs 2"), "shell 'info' -> 2 installs")

        # --- 6: trial, confirm, rollback ---
        fw.clear_text()
        fw.send_text("fw\r")
        check(fw.wait_text("on trial"), "v2 runs on trial (v1 kept as backup)")
        check(fw.confirm() is True, "CONFIRM ends the trial")
        check(fw.confirm() is False, "CONFIRM again: nothing to confirm")

        ver, _ = fw.upload(open(img3, "rb").read())
        fw.install()
        check(wait_role(fw, "app", "3.0.0") is not None, "v3 installed, running")
        check(fw.confirm() is True, "v3 confirmed")
        fw.simple(ak_fw.CMD_ROLLBACK, "ROLLBACK")
        check(wait_role(fw, "app", "2.1.0") is not None, "ROLLBACK -> v2 back")
        fw.clear_text()
        fw.send_text("fw\r")
        check(fw.wait_text("restored by a rollback"), "shell 'fw' says restored by a rollback")

        ver, _ = fw.upload(open(img3, "rb").read())
        fw.install()
        check(wait_role(fw, "app", "3.0.0") is not None, "v3 installed again, on trial")
        for i in range(3):
            fw.simple(ak_fw.CMD_RESET, "RESET")
            time.sleep(0.2)
            ok = wait_role(fw, "app") is not None
            check(ok, "reset %d of 3 without confirming" % (i + 1))
        check(wait_role(fw, "app", "2.1.0") is not None, "never confirmed -> bootloader rolled back to v2")
    finally:
        fw.close()

    # --- 7..8: restart, flash persisted ---
    fw = ak_fw.AkFw(ak_fw.SimLink([a.sim, "--flash", flash]))
    try:
        check(wait_role(fw, "app", "2.1.0") is not None, "restart -> straight into app v2")
        fw.simple(ak_fw.CMD_LOADER, "LOADER")
        check(wait_role(fw, "bootloader") is not None, "LOADER -> bootloader")
        fw.simple(ak_fw.CMD_RUN, "RUN")
        check(wait_role(fw, "app", "2.1.0") is not None, "RUN -> back to app v2")
    finally:
        fw.close()

    print("%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
