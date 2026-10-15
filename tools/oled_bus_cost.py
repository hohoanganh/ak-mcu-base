#!/usr/bin/env python3
"""OLED bus time per frame of the demo, from test_demo recordings.

Compares what gfx_flush() sends today (every changed page in full) with
sending only the changed column span of each page.

  build/host/test_demo <dir> record
  python tools/oled_bus_cost.py <dir> [--us-per-byte 40.7]

Cost model: one page = 5 command bytes (address, control, 3 position bytes)
+ 2 (address, control) + 128 data bytes = 135 bytes on the bus, measured at
about 5.5 ms on the kit with the bit-banged I2C of port/stm32l151/kit.c.
A span of n columns costs 5 + 2 + n bytes.
"""
import argparse
import glob
import os

PAGES, WIDTH = 8, 128
PAGE_OVERHEAD = 7


def scene_cost(frames):
	shown = bytes(PAGES * WIDTH)
	page_b = span_b = worst_page = worst_span = 0
	for fr in frames:
		pb = sb = 0
		for p in range(PAGES):
			a = fr[p * WIDTH:(p + 1) * WIDTH]
			b = shown[p * WIDTH:(p + 1) * WIDTH]
			if a == b:
				continue
			diff = [x for x in range(WIDTH) if a[x] != b[x]]
			pb += PAGE_OVERHEAD + WIDTH
			sb += PAGE_OVERHEAD + diff[-1] - diff[0] + 1
		shown = fr
		page_b += pb
		span_b += sb
		worst_page = max(worst_page, pb)
		worst_span = max(worst_span, sb)
	return page_b, span_b, worst_page, worst_span


def main():
	ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	ap.add_argument("dir", help="folder written by: test_demo <dir> record")
	ap.add_argument("--us-per-byte", type=float, default=5500 / 135.0)
	args = ap.parse_args()

	ms = args.us_per_byte / 1000.0
	print("%-9s %6s %9s %9s %10s %10s %6s" % ("scene", "frames", "page ms", "span ms", "worst page", "worst span", "gain"))
	for f in sorted(glob.glob(os.path.join(args.dir, "*.frames"))):
		d = open(f, "rb").read()
		frames = [d[i:i + PAGES * WIDTH] for i in range(0, len(d), PAGES * WIDTH)]
		if not frames:
			continue
		pb, sb, wp, ws = scene_cost(frames)
		n = len(frames)
		gain = (1 - sb / pb) * 100 if pb else 0
		print("%-9s %6d %9.1f %9.1f %10.1f %10.1f %5.0f%%" % (os.path.basename(f)[:-7], n, pb / n * ms, sb / n * ms,
															   wp * ms, ws * ms, gain))


if __name__ == "__main__":
	main()
