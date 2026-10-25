#!/usr/bin/env python3
"""rc_pc_sticks.py - drive the sticks of the rc_tx remote from the PC, for
testing the radio link without joysticks (docs/drone-ak-mcu.md, P1).

  python tools/rc_pc_sticks.py --port COM5                 # slow sweep of all axes
  python tools/rc_pc_sticks.py --port COM5 --still 0 0 0 0 # fixed sticks
  python tools/rc_pc_sticks.py --port COM5 --arm           # arm first (throttle at 0)

Sends "link stick r p y t" 10 times a second; the remote drops the sticks
(throttle 0, disarm) when they stop for 500 ms, so Ctrl+C is a safe stop.
Every 2 s it prints the "link" status of the remote. The throttle of the
sweep stays below 300 (of 1000).

Needs pyserial:  pip install pyserial
"""
import argparse
import math
import sys
import time


def main():
	ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	ap.add_argument("--port", required=True)
	ap.add_argument("--baud", type=int, default=115200)
	ap.add_argument("--still", type=int, nargs=4, metavar=("ROLL", "PITCH", "YAW", "THR"))
	ap.add_argument("--arm", action="store_true", help="arm after 1 s of throttle 0")
	ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = until Ctrl+C)")
	args = ap.parse_args()

	try:
		import serial
	except ImportError:
		sys.exit("needs pyserial: pip install pyserial")

	ser = serial.Serial(args.port, args.baud, timeout=0)
	t0 = time.time()
	next_status = t0 + 2
	armed_sent = False

	def send(line):
		ser.write((line + "\r").encode())

	try:
		while True:
			t = time.time() - t0
			if args.seconds and t > args.seconds:
				break
			if args.still:
				r, p, y, thr = args.still
			elif t < 1.5:
				r = p = y = thr = 0					# throttle at the bottom first: arming needs it
			else:
				r = int(300 * math.sin(t * 0.7))
				p = int(300 * math.sin(t * 0.9 + 1))
				y = int(200 * math.sin(t * 0.5 + 2))
				thr = int(150 + 120 * math.sin(t * 0.3))
			send("link stick %d %d %d %d" % (r, p, y, thr))
			if args.arm and not armed_sent and t > 1.0:
				send("link arm")
				armed_sent = True
			if time.time() >= next_status:
				send("link")
				next_status += 2
			out = ser.read(4096)
			if out:
				sys.stdout.write(out.decode(errors="replace").replace("> ", ""))
				sys.stdout.flush()
			time.sleep(0.1)
	except KeyboardInterrupt:
		pass
	send("link disarm")
	send("link stick off")
	time.sleep(0.2)
	ser.close()


if __name__ == "__main__":
	main()
