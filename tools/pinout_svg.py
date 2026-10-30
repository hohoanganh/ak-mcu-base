#!/usr/bin/env python3
"""
pinout_svg.py - draw the STM32L151CBT6 (LQFP-48) with what every pin does on
the AK Base Kit, as an SVG for the documentation.

  python tools/pinout_svg.py docs/kit/stm32l151-pinout.svg

The table below is the single place where the pins are written down for the
picture. It was checked against the schematic "AK MCU KIT 3.0" (sheet 7 MCU,
sheet 5 LCD, sheet 6 Connectors, 12 May 2026) and agrees with what the
firmware configures in port/stm32l151 (port_cfg.h, kit.c, rs485.c).
The connectors of the board are drawn by connectors_svg.py.
"""

import sys
from xml.sax.saxutils import escape

# group: (colour, legend text)
GROUPS = {
    "power":   ("#7c8a9a", "Nguồn, reset, boot"),
    "clock":   ("#b9a36b", "Thạch anh 8 MHz"),
    "debug":   ("#f5a623", "Nạp và debug (SWD)"),
    "console": ("#a8ff3e", "Console UART1: shell, OTA"),
    "rs485":   ("#6aa6ff", "RS485 trên UART2: Modbus, OTA, Pong"),
    "flash":   ("#c792ea", "Flash SPI: vùng chờ OTA, clip video"),
    "oled":    ("#22d3ee", "Màn hình OLED 128×64 (I2C mềm)"),
    "i2c":     ("#f472b6", "I2C1: RTC và các cổng I2C"),
    "button":  ("#facc15", "Ba nút bấm"),
    "out":     ("#fb7185", "Còi, LED"),
    "header":  ("#9fb3c8", "Ra cổng mở rộng, firmware chưa dùng"),
}

# pin number -> (name, what it does on the kit, group); LQFP-48, counter-clockwise from pin 1
PINS = {
    1: ("VLCD", "nguồn", "power"),
    2: ("PC13", "nút B2", "button"),
    3: ("PC14", "J13", "header"),
    4: ("PC15", "J13", "header"),
    5: ("PH0", "OSC_IN", "clock"),
    6: ("PH1", "OSC_OUT", "clock"),
    7: ("NRST", "nút RESET, J6", "power"),
    8: ("VSSA", "GND", "power"),
    9: ("VDDA", "3V3", "power"),
    10: ("PA0", "J13", "header"),
    11: ("PA1", "RS485 DIR", "rs485"),
    12: ("PA2", "RS485 TX (USART2)", "rs485"),
    13: ("PA3", "RS485 RX", "rs485"),
    14: ("PA4", "J6 (CSN)", "header"),
    15: ("PA5", "flash SCK", "flash"),
    16: ("PA6", "flash MISO", "flash"),
    17: ("PA7", "flash MOSI", "flash"),
    18: ("PB0", "còi (PWM)", "out"),
    19: ("PB1", "J6 (IRQ)", "header"),
    20: ("PB2", "BOOT1", "power"),
    21: ("PB10", "UART3 TX", "header"),
    22: ("PB11", "UART3 RX", "header"),
    23: ("VSS", "GND", "power"),
    24: ("VDD", "3V3", "power"),
    25: ("PB12", "OLED SDA", "oled"),
    26: ("PB13", "OLED SCL", "oled"),
    27: ("PB14", "flash CS", "flash"),
    28: ("PB15", "J13", "header"),
    29: ("PA8", "J6 (CE)", "header"),
    30: ("PA9", "console TX (USART1)", "console"),
    31: ("PA10", "console RX (USART1)", "console"),
    32: ("PA11", "J13", "header"),
    33: ("PA12", "J13", "header"),
    34: ("PA13", "SWDIO", "debug"),
    35: ("VSS", "GND", "power"),
    36: ("VDD", "3V3", "power"),
    37: ("PA14", "SWCLK", "debug"),
    38: ("PA15", "OLED RES, J13", "oled"),
    39: ("PB3", "nút B1", "button"),
    40: ("PB4", "nút B3", "button"),
    41: ("PB5", "J13", "header"),
    42: ("PB6", "I2C SCL", "i2c"),
    43: ("PB7", "I2C SDA", "i2c"),
    44: ("BOOT0", "nút BOOT0", "power"),
    45: ("PB8", "LED đỏ (thấp = sáng)", "out"),
    46: ("PB9", "J13", "header"),
    47: ("VSS", "GND", "power"),
    48: ("VDD", "3V3", "power"),
}

W, H = 1280, 1250
CHIP = 392                      # side of the package
CX, CY = W // 2, 600
PITCH = 28
STUB = 22                       # length of a pin
BG, CARD, LINE, TEXT, MUTED = "#0b0f14", "#111821", "#223041", "#e6edf3", "#8d9bab"
MONO = "ui-monospace, 'Cascadia Mono', Consolas, Menlo, monospace"
SANS = "system-ui, -apple-system, 'Segoe UI', sans-serif"


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "stm32l151-pinout.svg"
    left, top = CX - CHIP // 2, CY - CHIP // 2
    right, bottom = left + CHIP, top + CHIP
    first = (CHIP - 11 * PITCH) / 2.0           # from the corner to the first pin of a side
    s = []
    a = s.append
    a('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" role="img" '
      'aria-labelledby="t d">' % (W, H, W, H))
    a('<title id="t">Sơ đồ chân STM32L151CBT6 trên AK Base Kit</title>')
    a('<desc id="d">Vi điều khiển LQFP-48 nhìn từ trên, mỗi chân ghi tên và việc nó làm trên kit: console UART1, RS485, '
      'flash SPI, màn hình OLED, I2C, ba nút bấm, còi, LED, SWD.</desc>')
    a('<rect width="%d" height="%d" fill="%s"/>' % (W, H, BG))

    # title, top left corner
    a('<text x="48" y="64" fill="%s" font-family="%s" font-size="30" font-weight="700">STM32L151CBT6 trên AK Base Kit</text>'
      % (TEXT, SANS))
    a('<text x="48" y="96" fill="%s" font-family="%s" font-size="17">Chân nào làm việc gì trong firmware ak-mcu-base · đã đối chiếu với schematic AK MCU KIT 3.0</text>'
      % (MUTED, SANS))

    # the package
    a('<rect x="%d" y="%d" width="%d" height="%d" rx="14" fill="%s" stroke="%s" stroke-width="2"/>'
      % (left, top, CHIP, CHIP, CARD, "#3a4b5e"))
    a('<circle cx="%d" cy="%d" r="9" fill="none" stroke="%s" stroke-width="2"/>' % (left + 30, top + 30, MUTED))
    a('<text x="%d" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="27" font-weight="700">STM32L151CBT6</text>'
      % (CX, CY - 22, TEXT, MONO))
    a('<text x="%d" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="17">LQFP-48 · nhìn từ trên</text>'
      % (CX, CY + 8, MUTED, SANS))
    a('<text x="%d" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="17">Cortex-M3 · 32 MHz</text>'
      % (CX, CY + 36, MUTED, SANS))
    a('<text x="%d" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="17">flash 128K · RAM 32K · EEPROM 4K</text>'
      % (CX, CY + 62, MUTED, SANS))

    for n in range(1, 49):
        name, what, group = PINS[n]
        colour = GROUPS[group][0]
        side, k = (n - 1) // 12, (n - 1) % 12
        quiet = group in ("power", "clock")
        name_fill = TEXT
        label = '<tspan fill="%s" font-weight="700">%s</tspan>' % (name_fill, escape(name))
        if what:
            label += '<tspan fill="%s" dx="10">%s</tspan>' % (colour, escape(what))
        size = 16 if quiet else 17
        if side == 0:           # left, top to bottom
            y = top + first + k * PITCH
            a('<rect x="%d" y="%.1f" width="%d" height="12" rx="2" fill="%s"/>' % (left - STUB, y - 6, STUB, colour))
            a('<text x="%d" y="%.1f" fill="%s" font-family="%s" font-size="11" dominant-baseline="middle">%d</text>'
              % (left + 8, y + 1, MUTED, MONO, n))
            a('<text x="%d" y="%.1f" text-anchor="end" font-family="%s" font-size="%d" dominant-baseline="middle">%s</text>'
              % (left - STUB - 10, y + 1, MONO, size, label))
        elif side == 2:         # right, bottom to top
            y = bottom - first - k * PITCH
            a('<rect x="%d" y="%.1f" width="%d" height="12" rx="2" fill="%s"/>' % (right, y - 6, STUB, colour))
            a('<text x="%d" y="%.1f" text-anchor="end" fill="%s" font-family="%s" font-size="11" dominant-baseline="middle">%d</text>'
              % (right - 8, y + 1, MUTED, MONO, n))
            a('<text x="%d" y="%.1f" font-family="%s" font-size="%d" dominant-baseline="middle">%s</text>'
              % (right + STUB + 10, y + 1, MONO, size, label))
        elif side == 1:         # bottom, left to right; the text runs downwards
            x = left + first + k * PITCH
            a('<rect x="%.1f" y="%d" width="12" height="%d" rx="2" fill="%s"/>' % (x - 6, bottom, STUB, colour))
            a('<text x="%.1f" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="11">%d</text>'
              % (x, bottom - 8, MUTED, MONO, n))
            a('<text transform="translate(%.1f %d) rotate(90)" font-family="%s" font-size="%d" dominant-baseline="middle">%s</text>'
              % (x - 1, bottom + STUB + 10, MONO, size, label))
        else:                   # top, right to left; the text runs upwards
            x = right - first - k * PITCH
            a('<rect x="%.1f" y="%d" width="12" height="%d" rx="2" fill="%s"/>' % (x - 6, top - STUB, STUB, colour))
            a('<text x="%.1f" y="%d" text-anchor="middle" fill="%s" font-family="%s" font-size="11">%d</text>'
              % (x, top + 18, MUTED, MONO, n))
            a('<text transform="translate(%.1f %d) rotate(-90)" font-family="%s" font-size="%d" dominant-baseline="middle">%s</text>'
              % (x + 1, top - STUB - 10, MONO, size, label))

    # legend: two columns under the drawing
    keys = list(GROUPS)
    y0 = 1062
    for i, key in enumerate(keys):
        colour, text = GROUPS[key]
        x = 60 + (i // 6) * 620
        y = y0 + (i % 6) * 29
        a('<rect x="%d" y="%d" width="22" height="12" rx="2" fill="%s"/>' % (x, y - 10, colour))
        a('<text x="%d" y="%d" fill="%s" font-family="%s" font-size="17">%s</text>' % (x + 34, y + 1, TEXT, SANS, escape(text)))
    a('<line x1="48" y1="1024" x2="%d" y2="1024" stroke="%s"/>' % (W - 48, LINE))
    a('</svg>')
    open(out, "w", encoding="utf-8").write("\n".join(s) + "\n")
    print(out)


if __name__ == "__main__":
    main()
