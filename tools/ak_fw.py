#!/usr/bin/env python3
"""
ak_fw.py - flash / control an ak-mcu-base board over the fw_proto protocol.

Shares one UART with the text console: protocol frames (first byte 0xA5) are
split from log text; logs are printed with --verbose.

  ak_fw.py --port /dev/ttyUSB0 info
  ak_fw.py --port COM5 flash build/stm32l151/app.img      # OTA via app or loader
  ak_fw.py --port COM5 loader                             # app -> reset into bootloader
  ak_fw.py --sim build/host/ak_sim flash demo.img         # host simulator

--port requires pyserial:  pip install pyserial
"""

import argparse
import binascii
import os
import struct
import subprocess
import sys
import threading
import time

SOF = 0xA5
RESP = 0x80
CMD_INFO, CMD_BEGIN, CMD_DATA, CMD_END, CMD_INSTALL, CMD_LOADER, CMD_RESET, CMD_RUN, CMD_CONFIRM, CMD_ROLLBACK = range(1, 11)
MAX_CHUNK = 128

# keep in sync with services/fw/fw_types.h
ERRORS = {
    0: "ok", 1: "bad arg", 2: "bad state", 3: "too big", 4: "flash error", 5: "no image",
    6: "header crc", 7: "wrong board", 8: "wrong load addr", 9: "image crc", 10: "bad offset",
    11: "unknown cmd", 12: "bad size", 13: "bad vector table", 14: "bad signature",
}
ROLES = {0: "bootloader", 1: "app"}


class FwError(Exception):
    pass


def crc16(data):
    return binascii.crc_hqx(bytes(data), 0xFFFF)


def pack(cmd, seq, payload=b""):
    body = bytes([cmd, seq & 0xFF]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))


# ------------------------------------------------------------------------------
# Transport
# ------------------------------------------------------------------------------
class SerialLink:
    def __init__(self, port, baud):
        try:
            import serial  # noqa
        except ImportError:
            raise SystemExit("pyserial required: pip install pyserial")
        import serial
        self.s = serial.Serial(port, baud, timeout=0.05)

    def write(self, data):
        self.s.write(data)

    def read(self):
        # return what has arrived; read(4096) would sit out the whole port
        # timeout on every short response (50 ms per DATA chunk)
        return self.s.read(self.s.in_waiting or 1)

    def close(self):
        self.s.close()


class SimLink:
    """Runs ak_sim as a child process over stdin/stdout."""

    def __init__(self, sim_cmd):
        self.p = subprocess.Popen(sim_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, bufsize=0)
        os.set_blocking(self.p.stdout.fileno(), False)

    def write(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def read(self):
        try:
            d = self.p.stdout.read(4096)
        except BlockingIOError:
            d = None
        if not d:
            time.sleep(0.005)
            return b""
        return d

    def close(self):
        try:
            self.p.stdin.close()
        except OSError:
            pass
        try:
            self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.p.kill()


# ------------------------------------------------------------------------------
# Client
# ------------------------------------------------------------------------------
class AkFw:
    def __init__(self, link, verbose=False, log=None):
        self.link = link
        self.verbose = verbose
        self.seq = 0
        self.rx = bytearray()
        self.text = bytearray()     # received log text (non-frame bytes)
        self.frames = []            # (cmd, seq, payload)
        self.lock = threading.Lock()
        self.log = log or (lambda s: sys.stderr.write(s))
        self.stop = False
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()

    def close(self):
        self.stop = True
        self.t.join(timeout=1)
        self.link.close()

    def _reader(self):
        while not self.stop:
            try:
                d = self.link.read()
            except (OSError, ValueError):
                return
            if d:
                with self.lock:
                    self.rx += d
                    self._parse()

    def _parse(self):
        """Split self.rx into response frames and log text.

        EVERY 0xA5 in the buffer is tried as a start of frame, not only the
        first one. A stray 0xA5 in the log text right before a real frame
        "declares" a length made of bytes of that real frame; waiting for that
        many bytes would hold the real frame back for ever (it arrives, the
        request times out all the same).
        """
        rx = self.rx
        sof = bytes([SOF])
        while rx:
            i = rx.find(sof)
            if i < 0:
                self._text(rx)
                rx.clear()
                return
            if i:
                self._text(rx[:i])
                del rx[:i]

            hold = None         # first SOF whose frame may still be arriving
            found = None
            pos = 0
            while pos >= 0:
                left = len(rx) - pos
                if left < 7:
                    if hold is None:
                        hold = pos
                    break
                ln = rx[pos + 3] | (rx[pos + 4] << 8)
                if ln <= 512:
                    if left < 7 + ln:
                        if hold is None:
                            hold = pos
                    else:
                        fr = bytes(rx[pos:pos + 7 + ln])
                        if crc16(fr[1:5 + ln]) == (fr[5 + ln] | (fr[6 + ln] << 8)) and fr[1] & RESP:
                            found = (pos, fr)
                            break
                pos = rx.find(sof, pos + 1)

            if found:
                pos, fr = found
                if pos:
                    self._text(rx[:pos])        # what stood before it was text
                ln = len(fr) - 7
                self.frames.append((fr[1] & 0x7F, fr[2], fr[5:5 + ln]))
                del rx[:pos + len(fr)]
                continue
            if hold is None:                    # no frame can start in here
                self._text(rx)
                rx.clear()
            elif hold:
                self._text(rx[:hold])
                del rx[:hold]
            if len(rx) > 2048:                  # a false start must not hold text back for ever
                self._text(rx[:len(rx) - 1024])
                del rx[:len(rx) - 1024]
            return

    def _text(self, b):
        self.text += b
        if self.verbose:
            self.log(b.decode(errors="replace"))

    def send_text(self, s):
        self.link.write(s.encode())

    def wait_text(self, needle, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                if needle.encode() in self.text:
                    return True
            time.sleep(0.01)
        return False

    def clear_text(self):
        with self.lock:
            self.text.clear()

    def request(self, cmd, payload=b"", timeout=1.0, retries=3):
        for _ in range(retries):
            self.seq = (self.seq + 1) & 0xFF
            seq = self.seq
            self.link.write(pack(cmd, seq, payload))
            end = time.time() + timeout
            while time.time() < end:
                with self.lock:
                    for k, (c, s, p) in enumerate(self.frames):
                        if c == cmd and s == seq:
                            del self.frames[:k + 1]
                            return p
                time.sleep(0.002)
        raise FwError("no response to command 0x%02X" % cmd)

    @staticmethod
    def _check(p, what):
        if not p or p[0] != 0:
            code = p[0] if p else -1
            raise FwError("%s: %s" % (what, ERRORS.get(code, "error %d" % code)))

    def info(self, timeout=1.0, retries=3):
        p = self.request(CMD_INFO, timeout=timeout, retries=retries)
        self._check(p, "INFO")
        ver = (p[3], p[4], p[5])
        build = struct.unpack_from("<I", p, 7)[0]
        return {
            "proto": p[1],
            "role": ROLES.get(p[2], str(p[2])),
            "version": "%d.%d.%d" % ver,
            "build": build,
            "board": p[11:27].split(b"\0")[0].decode(errors="replace"),
            "staging_size": struct.unpack_from("<I", p, 27)[0],
            "max_chunk": p[31] | (p[32] << 8),
        }

    def wait_ready(self, timeout=10.0):
        """Wait until the board answers INFO (after reset / boot <-> app switch)."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                return self.info(timeout=0.3, retries=1)
            except FwError:
                pass
        raise FwError("no answer from board after %.0f s" % timeout)

    def upload(self, image, progress=None):
        """BEGIN + DATA + END. Returns (version, img_size) of the verified image."""
        info = self.info()
        chunk = min(MAX_CHUNK, info["max_chunk"] or MAX_CHUNK)
        if len(image) > info["staging_size"]:
            raise FwError("image %d B larger than staging %d B" % (len(image), info["staging_size"]))
        self._check(self.request(CMD_BEGIN, struct.pack("<I", len(image))), "BEGIN")
        off = 0
        while off < len(image):
            part = image[off:off + chunk]
            p = self.request(CMD_DATA, struct.pack("<I", off) + part, timeout=2.0)
            if p and p[0] == 10 and len(p) >= 5:     # bad offset: resync to the board
                off = struct.unpack_from("<I", p, 1)[0]
                continue
            self._check(p, "DATA @%d" % off)
            off += len(part)
            if progress:
                progress(off, len(image))
        p = self.request(CMD_END, timeout=5.0)
        self._check(p, "END")
        return "%d.%d.%d" % (p[1], p[2], p[3]), struct.unpack_from("<I", p, 9)[0]

    def install(self):
        self._check(self.request(CMD_INSTALL), "INSTALL")

    def simple(self, cmd, name):
        self._check(self.request(cmd), name)

    def confirm(self):
        """End the trial of the running image. Returns True if it was on trial."""
        p = self.request(CMD_CONFIRM)
        self._check(p, "CONFIRM")
        return len(p) > 1 and p[1] == 1


# ------------------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------------------
def _progress(done, total):
    pct = done * 100 // total
    sys.stderr.write("\r  %3d%%  %d/%d B" % (pct, done, total))
    if done == total:
        sys.stderr.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port (/dev/ttyUSB0, COM5)")
    g.add_argument("--sim", help="path to ak_sim (simulator)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--sim-flash", default="ak_sim_flash.bin")
    ap.add_argument("-v", "--verbose", action="store_true", help="print board log text")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    f = sub.add_parser("flash")
    f.add_argument("image")
    f.add_argument("--no-install", action="store_true", help="upload to staging only, do not install")
    sub.add_parser("loader")
    sub.add_parser("reset")
    sub.add_parser("run", help="bootloader: leave loader, run app")
    sub.add_parser("confirm", help="app: end the trial of the running image (else auto after APP_FW_CONFIRM_MS)")
    sub.add_parser("rollback", help="put the image before the last update back")
    a = ap.parse_args(argv)

    if a.port:
        link = SerialLink(a.port, a.baud)
    else:
        link = SimLink([a.sim, "--flash", a.sim_flash])
    fw = AkFw(link, verbose=a.verbose)
    try:
        info = fw.wait_ready(5.0)
        print("board %s, %s v%s build %d" % (info["board"], info["role"], info["version"], info["build"]))
        if a.cmd == "info":
            print("staging %d B, max chunk %d" % (info["staging_size"], info["max_chunk"]))
        elif a.cmd == "flash":
            image = open(a.image, "rb").read()
            t0 = time.time()
            ver, size = fw.upload(image, _progress)
            print("uploaded v%s (%d B) in %.1f s, verified on board" % (ver, size, time.time() - t0))
            if not a.no_install:
                fw.install()
                print("installing...")
                deadline = time.time() + 30.0
                time.sleep(0.3)
                info = fw.wait_ready(20.0)
                while info["role"] != "app" or info["version"] != ver:
                    if time.time() > deadline:
                        raise FwError("still %s v%s after install (see log: -v)" % (info["role"], info["version"]))
                    time.sleep(0.2)
                    info = fw.wait_ready(20.0)
                print("running %s v%s" % (info["role"], info["version"]))
        elif a.cmd == "loader":
            fw.simple(CMD_LOADER, "LOADER")
            time.sleep(0.3)
            print("now in %s" % fw.wait_ready(10.0)["role"])
        elif a.cmd == "reset":
            fw.simple(CMD_RESET, "RESET")
        elif a.cmd == "run":
            fw.simple(CMD_RUN, "RUN")
        elif a.cmd == "confirm":
            print("confirmed" if fw.confirm() else "nothing to confirm (image not on trial)")
        elif a.cmd == "rollback":
            fw.simple(CMD_ROLLBACK, "ROLLBACK")
            print("rolling back...")
            time.sleep(0.3)
            info = fw.wait_ready(20.0)
            print("running %s v%s" % (info["role"], info["version"]))
    except FwError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        fw.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
