#!/usr/bin/env python3
"""
mkimage.py - build / patch / inspect ak-mcu-base firmware images:
             [256 B header][binary].

  patch      .bin that already carries a placeholder header (.fw_header
             section of the app .elf) -> fill img_size + CRCs, write .img;
             optionally update the .elf so a SWD flash is a valid image too.
  build      plain .bin (no header) -> prepend a header.
  synthetic  fake image (sane vector table + random data) for ak_sim / tests.
  info       print and verify an .img file.

Header (little-endian, see services/fw/fw_image.h):
  0 magic 'AKFW' | 4 hdr_version u16 | 6 hdr_size u16 | 8 img_size | 12 img_crc32
  16 load_addr | 20 ver major,minor,patch,0 | 24 build u32 | 28 board[16]
  44 sig_type | 48 sig[64] | 112 reserved[140] | 252 hdr_crc32

--sign-key priv.pem (patch / build / synthetic): ECDSA P-256 signature over
SHA-256(header[0:48] + binary), for boards built with signed images
(tools/ak_sign.py makes the keys).
"""

import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MAGIC = 0x57464B41
HDR_VERSION = 1
HDR_SIZE = 256
BOARD_LEN = 16


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_version(s):
    parts = s.split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("version must be major.minor.patch")
    v = tuple(int(p) for p in parts)
    if any(x < 0 or x > 255 for x in v):
        raise argparse.ArgumentTypeError("each version field must be 0..255")
    return v


SIG_TYPE_OFF = 44
SIG_OFF = 48
SIG_ECDSA_P256 = 1


def pack_header(img, load_addr, version, build, board, sign_key=None):
    board_b = board.encode()
    if len(board_b) > BOARD_LEN:
        raise SystemExit("board name: max %d chars" % BOARD_LEN)
    hdr = struct.pack("<IHHIII4BI16s", MAGIC, HDR_VERSION, HDR_SIZE, len(img), crc32(img),
                      load_addr, version[0], version[1], version[2], 0, build,
                      board_b.ljust(BOARD_LEN, b"\0"))
    if sign_key:
        import ak_sign
        hdr += bytes([SIG_ECDSA_P256, 0, 0, 0])
        hdr = hdr.ljust(HDR_SIZE - 4, b"\0")
        hdr = ak_sign.sign_blob(hdr, img, ak_sign.load_private(sign_key))
    hdr = hdr.ljust(HDR_SIZE - 4, b"\0")
    return hdr + struct.pack("<I", crc32(hdr))


def parse_header(blob):
    if len(blob) < HDR_SIZE:
        raise ValueError("file shorter than header")
    (magic, hv, hs, size, img_crc, load, ma, mi, pa, _r, build, board) = struct.unpack_from(
        "<IHHIII4BI16s", blob, 0)
    return {
        "magic": magic, "hdr_version": hv, "hdr_size": hs, "img_size": size,
        "img_crc32": img_crc, "load_addr": load, "version": (ma, mi, pa), "build": build,
        "board": board.split(b"\0")[0].decode(errors="replace"),
        "hdr_crc32": struct.unpack_from("<I", blob, HDR_SIZE - 4)[0],
    }


def check_image(blob):
    """Returns (header dict, list of errors)."""
    errs = []
    h = parse_header(blob)
    if h["magic"] != MAGIC:
        errs.append("bad magic")
    if crc32(blob[:HDR_SIZE - 4]) != h["hdr_crc32"]:
        errs.append("bad hdr_crc32")
    if h["hdr_version"] != HDR_VERSION or h["hdr_size"] != HDR_SIZE:
        errs.append("unknown hdr_version/hdr_size")
    body = blob[HDR_SIZE:HDR_SIZE + h["img_size"]]
    if len(body) != h["img_size"]:
        errs.append("file shorter than img_size")
    elif crc32(body) != h["img_crc32"]:
        errs.append("bad img_crc32")
    if len(body) >= 8:
        sp, rst = struct.unpack_from("<II", body, 0)
        if not rst & 1:
            errs.append("reset handler 0x%08X lacks the Thumb bit" % rst)
        if not (h["load_addr"] < rst < h["load_addr"] + 0x100000):
            errs.append("reset handler 0x%08X outside the app" % rst)
    return h, errs


def cmd_patch(a):
    blob = open(a.input, "rb").read()
    h = parse_header(blob)
    if h["magic"] != MAGIC:
        raise SystemExit("%s: no placeholder header at file start (is .fw_header linked?)" % a.input)
    img = blob[HDR_SIZE:]
    version = a.version or h["version"]
    build = a.build if a.build is not None else h["build"]
    board = a.board or h["board"]
    out = pack_header(img, h["load_addr"], version, build, board, a.sign_key) + img
    open(a.output, "wb").write(out)
    if a.elf:
        with tempfile.NamedTemporaryFile(suffix=".hdr", delete=False) as f:
            f.write(out[:HDR_SIZE])
            tmp = f.name
        try:
            subprocess.check_call([a.objcopy, "--update-section", ".fw_header=" + tmp, a.elf])
        finally:
            os.unlink(tmp)
    print("%s: v%d.%d.%d build %d, board %s, %d B, crc32 %08X%s" % (
        a.output, version[0], version[1], version[2], build, board, len(img), crc32(img),
        ", signed" if a.sign_key else ""))


def cmd_build(a):
    img = open(a.input, "rb").read()
    out = pack_header(img, a.load_addr, a.version, a.build, a.board, a.sign_key) + img
    open(a.output, "wb").write(out)
    print("%s: %d B%s" % (a.output, len(out), ", signed" if a.sign_key else ""))


def cmd_synthetic(a):
    rnd = random.Random(a.seed)
    body = bytearray(rnd.getrandbits(8) for _ in range(a.size))
    struct.pack_into("<II", body, 0, a.ram_end, a.load_addr + 0x101)
    out = pack_header(bytes(body), a.load_addr, a.version, a.build, a.board, a.sign_key) + bytes(body)
    open(a.output, "wb").write(out)
    print("%s: synthetic v%d.%d.%d, %d B" % ((a.output,) + a.version + (len(out),)))


def cmd_info(a):
    blob = open(a.input, "rb").read()
    h, errs = check_image(blob)
    print("file       %s (%d B)" % (a.input, len(blob)))
    print("version    %d.%d.%d build %d" % (h["version"] + (h["build"],)))
    print("board      %s" % h["board"])
    print("load_addr  0x%08X" % h["load_addr"])
    print("img_size   %d" % h["img_size"])
    print("img_crc32  %08X" % h["img_crc32"])
    sig_type = blob[SIG_TYPE_OFF] if len(blob) > SIG_TYPE_OFF else 0
    if sig_type == SIG_ECDSA_P256:
        if a.pub:
            import ak_sign
            errs += [] if ak_sign.verify_blob(blob, ak_sign.load_public(a.pub)) else ["bad signature"]
            print("signature  ECDSA P-256, %s" % ("OK" if not errs or errs[-1] != "bad signature" else "BAD"))
        else:
            print("signature  ECDSA P-256 (not checked: give --pub)")
    else:
        print("signature  none")
    print("status     %s" % ("OK" if not errs else "; ".join(errs)))
    return 0 if not errs else 1


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("patch", help="fill CRCs into a .bin with placeholder header")
    s.add_argument("input")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--elf", help="also update .fw_header in this .elf")
    s.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    s.add_argument("--version", type=parse_version)
    s.add_argument("--build", type=int)
    s.add_argument("--board")
    s.add_argument("--sign-key", help="private key (PEM): sign the image")
    s.set_defaults(fn=cmd_patch)

    s = sub.add_parser("build", help="prepend a header to a plain .bin")
    s.add_argument("input")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--load-addr", type=lambda x: int(x, 0), required=True)
    s.add_argument("--version", type=parse_version, required=True)
    s.add_argument("--build", type=int, default=0)
    s.add_argument("--board", required=True)
    s.add_argument("--sign-key", help="private key (PEM): sign the image")
    s.set_defaults(fn=cmd_build)

    s = sub.add_parser("synthetic", help="fake image for ak_sim / tests")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--size", type=int, default=8192)
    s.add_argument("--load-addr", type=lambda x: int(x, 0), default=0x08003100)
    s.add_argument("--ram-end", type=lambda x: int(x, 0), default=0x20004000)
    s.add_argument("--version", type=parse_version, default=(1, 0, 0))
    s.add_argument("--build", type=int, default=0)
    s.add_argument("--board", default="ak-host")
    s.add_argument("--seed", type=int, default=1)
    s.add_argument("--sign-key", help="private key (PEM): sign the image")
    s.set_defaults(fn=cmd_synthetic)

    s = sub.add_parser("info", help="print + verify an image")
    s.add_argument("input")
    s.add_argument("--pub", help="public key (PEM): also check the signature")
    s.set_defaults(fn=cmd_info)

    a = p.parse_args(argv)
    return a.fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
