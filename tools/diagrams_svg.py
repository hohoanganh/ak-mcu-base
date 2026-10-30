#!/usr/bin/env python3
"""
diagrams_svg.py - the block diagrams of the documentation, as SVG.

  python tools/diagrams_svg.py            # writes into docs/kit and docs/

Hardware (from the schematic "AK MCU KIT 3.0", 12 May 2026):
  docs/kit/block-diagram.svg     what is on the board and how it reaches the MCU
  docs/kit/power-tree.svg        where every supply rail comes from
Software (from the sources of ak-mcu-base):
  docs/diagram-layers.svg       the layers, and the three places the same code runs
  docs/diagram-memory.svg       internal flash, SPI flash, EEPROM
  docs/diagram-ota.svg          a firmware update, step by step
  docs/diagram-frame-loop.svg   how the demo sits on the kernel
  docs/diagram-pong.svg         two kits on one RS485 line

Same look as pinout_svg.py and connectors_svg.py.
"""

import os
from xml.sax.saxutils import escape

from pinout_svg import GROUPS, BG, CARD, LINE, TEXT, MUTED, MONO, SANS

ACCENT = "#a8ff3e"
W = 1280
HERE = os.path.dirname(os.path.abspath(__file__))
KIT_DIR = os.path.normpath(os.path.join(HERE, "..", "docs", "kit"))
DOC_DIR = os.path.normpath(os.path.join(HERE, "..", "docs"))


def colour(group):
    return GROUPS[group][0]


class Svg:
    def __init__(self, height, title, subtitle, desc):
        self.h = height
        self.s = []
        self.add('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" role="img" '
                 'aria-labelledby="t d">' % (W, height, W, height))
        self.add('<title id="t">%s</title><desc id="d">%s</desc>' % (escape(title), escape(desc)))
        self.add('<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
                 '<path d="M0 0L10 5L0 10z" fill="%s"/></marker></defs>' % MUTED)
        self.add('<rect width="%d" height="%d" fill="%s"/>' % (W, height, BG))
        self.text(48, 64, title, 30, TEXT, weight=700)
        self.text(48, 96, subtitle, 17, MUTED)

    def add(self, x):
        self.s.append(x)

    def text(self, x, y, t, size=16, fill=TEXT, anchor="start", weight=400, mono=False):
        self.add('<text x="%.1f" y="%.1f" fill="%s" font-family="%s" font-size="%d" font-weight="%d" text-anchor="%s">%s</text>'
                 % (x, y, fill, MONO if mono else SANS, size, weight, anchor, escape(t)))

    def box(self, x, y, w, h, title, lines=(), group=None, fill=CARD, title_size=18):
        """A card with a coloured bar on its left edge, a title and small lines under it."""
        stroke = colour(group) if group else "#3a4b5e"
        self.add('<rect x="%d" y="%d" width="%d" height="%d" rx="8" fill="%s" stroke="%s" stroke-width="1.5"/>'
                 % (x, y, w, h, fill, stroke))
        if group:
            self.add('<rect x="%d" y="%d" width="6" height="%d" rx="3" fill="%s"/>' % (x, y, h, stroke))
        ty = y + (h - (len(lines) * 19)) / 2 + (8 if lines else 6)
        self.text(x + w / 2 + (3 if group else 0), ty, title, title_size, TEXT, "middle", 700)
        for i, line in enumerate(lines):
            self.text(x + w / 2 + (3 if group else 0), ty + 21 + i * 19, line, 14, MUTED, "middle", mono=False)

    def line(self, points, arrow=True, both=False, dash=False, stroke=MUTED, width=2):
        d = " ".join("%s%.1f %.1f" % ("M" if i == 0 else "L", px, py) for i, (px, py) in enumerate(points))
        extra = ' marker-end="url(#a)"' if arrow else ""
        if both:
            extra += ' marker-start="url(#a)"'
        if dash:
            extra += ' stroke-dasharray="6 5"'
        self.add('<path d="%s" fill="none" stroke="%s" stroke-width="%s"%s/>' % (d, stroke, width, extra))

    def label(self, x, y, t, fill=MUTED, anchor="middle", size=13):
        w = len(t) * size * 0.58 + 10
        x0 = x - w / 2 if anchor == "middle" else (x - 5 if anchor == "start" else x - w + 5)
        self.add('<rect x="%.1f" y="%.1f" width="%.1f" height="%d" rx="3" fill="%s"/>' % (x0, y - size, w, size + 6, BG))
        self.text(x, y, t, size, fill, anchor, mono=True)

    def save(self, path):
        self.add('</svg>')
        open(path, "w", encoding="utf-8").write("\n".join(self.s) + "\n")
        print(os.path.relpath(path, os.path.join(HERE, "..", "..")), W, self.h)


# ------------------------------------------------------------------------------
# hardware
# ------------------------------------------------------------------------------
def block_diagram():
    g = Svg(800, "AK Base Kit: trên bo có gì", "Mỗi khối nối với vi điều khiển bằng đường nào · theo schematic AK MCU KIT 3.0",
            "Sơ đồ khối của bo: vi điều khiển STM32L151CBT6 ở giữa, quanh nó là nguồn, mạch USB-UART, cổng nạp, màn hình OLED, "
            "flash SPI, RTC, RS485, ba nút bấm, còi, LED và các cổng mở rộng.")
    mx, my, mw, mh = 500, 250, 280, 330
    g.add('<rect x="%d" y="%d" width="%d" height="%d" rx="12" fill="%s" stroke="%s" stroke-width="2"/>' % (mx, my, mw, mh, "#16202b", ACCENT))
    g.text(mx + mw / 2, my + mh / 2 - 26, "STM32L151CBT6", 24, TEXT, "middle", 700, mono=True)
    g.text(mx + mw / 2, my + mh / 2 + 2, "Cortex-M3 · 32 MHz", 16, MUTED, "middle")
    g.text(mx + mw / 2, my + mh / 2 + 26, "flash 128K · RAM 32K", 16, MUTED, "middle")
    g.text(mx + mw / 2, my + mh / 2 + 50, "EEPROM 4K", 16, MUTED, "middle")

    # left: how the board talks to a PC
    left = [
        (285, "USB Type-C", ["5V vào, dữ liệu USB"], "power", "", None),
        (385, "CH340E", ["USB ↔ UART"], "console", "UART1  PA9 PA10", "console"),
        (485, "Cổng nạp J14, J15", ["SWD cho ST-Link"], "debug", "SWD  PA13 PA14", "debug"),
        (585, "Thạch anh 8 MHz", [], "clock", "", "clock"),
    ]
    for cy, title, lines, group, lab, link in left:
        g.box(60, cy - 34, 260, 68, title, lines, group)
        if link:
            g.line([(320, cy), (mx, cy)], both=(link != "clock"), stroke=colour(link))
            if lab:
                g.label(410, cy - 8, lab, colour(link))
    g.line([(190, 319), (190, 351)], stroke=colour("console"))          # USB data down into the bridge
    g.label(196, 340, "D+ D−", colour("console"), "start")

    # right: what is soldered on the board
    right = [
        (280, "OLED 1,54\" SSD1309", ["128×64 · I2C 0x3C"], "oled", "I2C mềm PB13 PB12 PA15"),
        (372, "Flash SPI W25Q80", ["1 MB"], "flash", "SPI1 PA5 PA6 PA7 PB14"),
        (464, "RTC PCF85063", ["0x51 · pin CR1220"], "i2c", "I2C1  PB6 PB7"),
        (556, "RS485 SP3485", ["ra J10 (A, B)"], "rs485", "UART2 PA2 PA3 · DIR PA1"),
    ]
    for cy, title, lines, group, lab in right:
        g.box(960, cy - 36, 260, 72, title, lines, group)
        g.line([(mx + mw, cy), (960, cy)], both=True, stroke=colour(group))
        g.label(870, cy - 8, lab, colour(group))

    # top: expansion
    top = [
        (290, "J13 · chân vi điều khiển", ["PA0 PA11 PA12 PA15 PB5 PB9", "PB15 PC14 PC15"], 560),
        (640, "J6 · SPI mở rộng", ["SPI1 + PA4 PA8 PB1", "cho nRF24L01+, W5500"], 640),
        (990, "J4 J7 J9 · UART3, I2C1", ["PB10 PB11 · PB6 PB7", "chuẩn Qwiic"], 720),
    ]
    for cx, title, lines, tx in top:
        g.box(cx - 150, 120, 300, 82, title, lines, "header", title_size=16)
        g.line([(cx, 202), (cx, 226), (tx, 226), (tx, my)], both=True, stroke=colour("header"))

    # bottom: things a person touches or hears
    bottom = [
        (250, "Ba nút bấm", ["PB3 · PC13 · PB4"], "button", 540),
        (490, "Nút RESET, BOOT0", ["NRST · BOOT0"], "power", 600),
        (790, "Còi", ["PB0 · PWM · qua transistor, 5V"], "out", 680),
        (1040, "LED đỏ", ["PB8 · mức thấp = sáng"], "out", 740),
    ]
    for cx, title, lines, group, tx in bottom:
        g.box(cx - 115, 690, 230, 70, title, lines, group, title_size=16)
        g.line([(cx, 690), (cx, 640), (tx, 640), (tx, my + mh)], both=False, stroke=colour(group))
    g.save(os.path.join(KIT_DIR, "block-diagram.svg"))


def power_tree():
    g = Svg(520, "Cây nguồn", "Mỗi đường nguồn trên bo lấy từ đâu · theo schematic AK MCU KIT 3.0",
            "Cây nguồn: USB Type-C 5V qua cầu chì và diode thành đường 5V; LDO RT9013 tạo 3V3 cho vi điều khiển và các IC; "
            "mạch tăng áp TPS61040 tạo 12,5V cho màn hình OLED; còi dùng 5V.")
    g.box(48, 250, 190, 80, "USB Type-C", ["5V · tối đa 1 A"], "power")
    g.box(290, 250, 190, 80, "Bảo vệ", ["cầu chì F1 · diode D4"], "power")
    g.line([(238, 290), (290, 290)])
    g.line([(480, 290), (560, 290)])
    g.label(520, 282, "5V", TEXT)
    # the 5 V rail
    g.line([(560, 180), (560, 440)], arrow=False, stroke=TEXT, width=3)
    g.box(620, 150, 220, 72, "LDO RT9013-33", ["5V → 3V3"], "console")
    g.line([(560, 186), (620, 186)])
    g.box(620, 396, 220, 72, "Còi", ["5V, đóng cắt bằng transistor"], "out")
    g.line([(560, 432), (620, 432)])
    g.box(620, 274, 220, 72, "Nguồn ra cổng nối", ["J12 (SB7) · J9 · tuỳ chọn J4, J7"], "header")
    g.line([(560, 310), (620, 310)])
    # the 3V3 rail
    g.line([(840, 186), (910, 186)])
    g.label(875, 178, "3V3", TEXT)
    g.line([(910, 130), (910, 330)], arrow=False, stroke=ACCENT, width=3)
    loads = [(140, "Vi điều khiển, flash SPI, RTC"), (200, "RS485 SP3485 · CH340E"), (260, "Nút bấm, LED, cổng nối 3V3")]
    for y, t in loads:
        g.box(970, y - 24, 262, 48, t, [], None, title_size=15)
        g.line([(910, y), (970, y)])
    g.box(970, 300, 262, 72, "Tăng áp TPS61040", ["3V3 → 12,5V cho tấm OLED"], "oled")
    g.line([(910, 326), (970, 326)])
    g.text(48, 486, "Pin CR1220 chỉ nuôi RTC khi mất nguồn. Bo không có đường nguồn nào khác ngoài USB Type-C.", 15, MUTED)
    g.save(os.path.join(KIT_DIR, "power-tree.svg"))


# ------------------------------------------------------------------------------
# software
# ------------------------------------------------------------------------------
def layers():
    g = Svg(640, "Một mã nguồn, ba nơi chạy", "Mọi thứ phía trên lớp HAL không biết mình đang chạy trên chip nào",
            "Các lớp của ak-mcu-base: ứng dụng và demo, kernel và dịch vụ, lớp HAL; bên dưới là ba port: chip STM32L151, "
            "máy tính để chạy unit test, và trình duyệt bằng WebAssembly.")
    x, w = 48, W - 96
    g.box(x, 130, w, 78, "Ứng dụng", ["app/: shell, OTA, Modbus, giám sát task   ·   demo/: 16 màn hình trên OLED"], "console")
    g.box(x, 222, w, 78, "Kernel AK và dịch vụ", ["kernel/: task, message, timer   ·   services/: cập nhật firmware, nhật ký sự cố, Modbus"], "rs485")
    g.box(x, 314, w, 70, "HAL: ranh giới duy nhất với phần cứng", ["hal.h · hal_flash.h · hal_rs485.h · demo/kit.h"], "i2c")
    g.text(48 + (w - 48) / 3 + 12, 417, "cùng một mã C ở trên", 15, MUTED, "middle")
    g.text(48 + 2 * ((w - 48) / 3 + 24) - 12, 417, "chỉ đổi lớp dưới đây", 15, MUTED, "middle")
    cw = (w - 48) / 3
    ports = [
        ("port/stm32l151", "Chip thật", ["SPL, startup, linker", "→ file .img nạp vào kit"], "out"),
        ("port/host", "Máy tính", ["flash và EEPROM giả lập, cắt điện giả", "→ unit test với ASan, giả lập OTA"], "button"),
        ("port/web", "Trình duyệt", ["màn hình, nút, còi thành trang web", "→ WebAssembly, chạy thử không cần kit"], "oled"),
    ]
    for i, (path, title, lines, group) in enumerate(ports):
        px = x + i * (cw + 24)
        g.line([(px + cw / 2, 384), (px + cw / 2, 440)], both=True)
        g.box(px, 440, cw, 150, title, [path] + lines, group, title_size=20)
    g.save(os.path.join(DOC_DIR, "diagram-layers.svg"))


def bar(g, x, y, w, parts, title, right):
    """One memory as a row of segments: parts = [(share of the width, name, detail, group)]."""
    g.text(x, y - 14, title, 18, TEXT, weight=700)
    g.text(x + w, y - 14, right, 14, MUTED, "end", mono=True)
    px = x
    for share, name, detail, group in parts:
        pw = w * share
        fill = colour(group) if group else "#1b2632"
        g.add('<rect x="%.1f" y="%d" width="%.1f" height="74" fill="%s" stroke="%s" stroke-width="2"/>' % (px, y, pw, fill, BG))
        ink = BG if group else MUTED
        g.text(px + pw / 2, y + 32, name, 16, ink, "middle", 700)
        g.text(px + pw / 2, y + 54, detail, 13, ink, "middle", mono=True)
        px += pw


def memory():
    g = Svg(600, "Bản đồ bộ nhớ", "Firmware đặt cái gì ở đâu trên STM32L151CB và flash SPI của kit",
            "Flash trong 128K chia cho bootloader 12K và app 116K; flash SPI 1 MB có kho clip 512K, vùng chờ OTA 116K "
            "và bản sao app cũ 116K để quay lui; EEPROM giữ trạng thái cài đặt và nhật ký sự cố.")
    x, w = 48, W - 96
    bar(g, x, 160, w, [(0.16, "BOOT", "12K · 0x08000000", "debug"), (0.84, "APP", "116K · 0x08003000 · 256 byte đầu là header (CRC32, tên board)", "console")],
        "Flash trong vi điều khiển", "128K · 0x08000000 – 0x0801FFFF")
    bar(g, x, 310, w, [(0.44, "Kho clip của demo", "512K · 0x00000 · log thời tiết và chương trình khối ở cuối", "oled"),
                       (0.2, "Vùng chờ OTA", "116K · 0x80000", "flash"), (0.2, "App cũ (quay lui)", "116K · 0xA0000", "flash"),
                       (0.16, "chưa dùng", "268K · 0xBD000", None)],
        "Flash SPI W25Q80 trên bo", "1 MB · 0x00000 – 0xFFFFF")
    bar(g, x, 460, w, [(0.26, "Trạng thái cài đặt", "2 ô × 32 byte (lệnh, lần thử, đang thử)", "button"), (0.4, "Nhật ký sự cố", "8 bản ghi × 20 byte", "out"),
                       (0.34, "chưa dùng", "", None)],
        "EEPROM trong vi điều khiển", "firmware dùng 224 byte đầu của 4K")
    g.text(x, 572, "Chiều rộng các ô không theo tỉ lệ. App chỉ được ghi vùng chờ OTA; chỉ bootloader được ghi APP và app cũ.", 15, MUTED)
    g.save(os.path.join(DOC_DIR, "diagram-memory.svg"))


def ota():
    g = Svg(560, "Một lần cập nhật firmware", "Từ máy tính tới lúc app mới chạy · qua UART hoặc RS485",
            "Năm bước của một lần cập nhật: máy tính gửi ảnh, app ghi vào vùng chờ, app kiểm ảnh, app ghi yêu cầu cài rồi reset, "
            "bootloader cài và chạy app mới. Mất điện ở bước nào thiết bị cũng không hỏng.")
    steps = [
        ("1", "Máy tính gửi ảnh", ["ak_fw.py qua UART", "ak_mb.py qua RS485"], "console"),
        ("2", "App ghi vùng chờ", ["trên flash SPI", "app vẫn chạy bình thường"], "flash"),
        ("3", "App kiểm ảnh", ["CRC32 · tên board", "địa chỉ nạp · bảng vector"], "flash"),
        ("4", "Ghi yêu cầu, reset", ["hai ô có số thứ tự", "trong EEPROM"], "button"),
        ("5", "Bootloader cài", ["xoá APP, chép, kiểm CRC", "rồi chạy app mới"], "debug"),
    ]
    bw, gap = 216, 26
    x = 48
    for i, (num, title, lines, group) in enumerate(steps):
        g.box(x, 150, bw, 130, title, lines, group)
        g.add('<circle cx="%d" cy="150" r="17" fill="%s"/>' % (x + 26, colour(group)))
        g.text(x + 26, 156, num, 17, BG, "middle", 700, mono=True)
        if i < len(steps) - 1:
            g.line([(x + bw, 215), (x + bw + gap, 215)])
        x += bw + gap
    # what a power cut does, under the steps it belongs to
    span1 = 3 * bw + 2 * gap
    g.add('<rect x="48" y="326" width="%d" height="96" rx="8" fill="%s" stroke="%s"/>' % (span1, CARD, LINE))
    g.text(48 + span1 / 2, 360, "Mất điện ở bước 1–3", 17, TEXT, "middle", 700)
    g.text(48 + span1 / 2, 386, "App cũ còn nguyên và chạy lại như chưa có gì.", 15, MUTED, "middle")
    g.text(48 + span1 / 2, 408, "Vùng APP chưa hề bị đụng tới: gửi lại ảnh là xong.", 15, MUTED, "middle")
    x2 = 48 + span1 + gap
    span2 = 2 * bw + gap
    g.add('<rect x="%d" y="326" width="%d" height="96" rx="8" fill="%s" stroke="%s"/>' % (x2, span2, CARD, LINE))
    g.text(x2 + span2 / 2, 360, "Mất điện ở bước 4–5", 17, TEXT, "middle", 700)
    g.text(x2 + span2 / 2, 386, "Bootloader thấy việc cài còn dở", 15, MUTED, "middle")
    g.text(x2 + span2 / 2, 408, "và làm lại từ đầu, từ ảnh trong vùng chờ.", 15, MUTED, "middle")
    for cx in (48 + span1 / 2, x2 + span2 / 2):
        g.line([(cx, 280), (cx, 326)], arrow=False, dash=True)
    g.text(48, 470, "Unit test giả lập cắt điện ở 1366 điểm trong bước 5: lần nào thiết bị cũng khởi động lại được.", 15, MUTED)
    g.text(48, 496, "Bootloader có watchdog 10 giây, nên một lần cài bị treo cũng tự reset và làm lại.", 15, MUTED)
    g.save(os.path.join(DOC_DIR, "diagram-ota.svg"))


def frame_loop():
    g = Svg(600, "Demo chạy trên kernel như thế nào", "Không có vòng lặp chờ: mọi việc là một message tới một task",
            "Timer của kernel gửi message khung cho task giao diện 20 lần mỗi giây; task gọi màn hình đang mở để vẽ vào bộ đệm khung, "
            "rồi chỉ gửi những trang đã đổi ra màn hình. Nút bấm và các task khác cũng gửi message theo cách đó.")
    # sources of messages
    src = [
        (160, "Timer 50 ms", ["UI_SIG_FRAME"], "clock"),
        (262, "Polling nút bấm", ["chống dội → UI_SIG_KEY_x"], "button"),
        (364, "Timer một lần", ["tắt bíp · nốt nhạc kế"], "out"),
    ]
    for y, title, lines, group in src:
        g.box(48, y - 40, 230, 80, title, lines, group, title_size=17)
        g.line([(278, y), (360, 262)], stroke=colour(group))
    g.box(360, 212, 170, 100, "Hàng đợi", ["message của", "task_ui"], None)
    g.line([(530, 262), (590, 262)])
    g.box(590, 202, 190, 120, "task_ui", ["chạy hết một", "message rồi nhường"], "console", title_size=20)
    g.line([(780, 262), (840, 262)])
    g.box(840, 212, 180, 100, "Màn hình đang mở", ["frame(): cập nhật", "rồi vẽ lại"], "oled", title_size=16)
    g.line([(1020, 262), (1066, 262)])
    g.box(1066, 212, 166, 100, "Bộ đệm khung", ["1024 byte", "8 trang × 128"], "flash", title_size=16)
    g.line([(1149, 312), (1149, 400)])
    g.box(1010, 400, 222, 84, "gfx_flush", ["chỉ gửi trang đã đổi", "≈ 5,5 ms mỗi trang"], "i2c", title_size=17)
    g.line([(1010, 442), (940, 442)])
    g.box(760, 400, 180, 84, "OLED", ["I2C mềm"], "oled", title_size=17)
    # the others
    g.box(360, 400, 340, 84, "Shell, OTA, Modbus", ["task ưu tiên cao hơn: chen vào giữa hai khung"], "rs485", title_size=17)
    g.line([(530, 400), (640, 322)], dash=True)
    g.text(48, 540, "Một khung phải xong trong 50 ms. Khung nào chậm hơn thì khung kế chỉ bị lùi lại: timer khung được đặt lại sau mỗi lượt,", 15, MUTED)
    g.text(48, 564, "nên message không dồn vào pool.", 15, MUTED)
    g.save(os.path.join(DOC_DIR, "diagram-frame-loop.svg"))


def pong():
    g = Svg(600, "Pong qua RS485: hai kit trên một đôi dây", "Ai nói lúc nào trên đường dây bán song công · 9600 8N1",
            "Hai kit nối A với A, B với B. Kit chủ gửi khung trạng thái 11 byte mỗi 50 ms, kit khách trả lời ngay bằng khung 6 byte; "
            "phần còn lại của 50 ms đường dây im lặng.")
    g.box(48, 140, 300, 130, "Kit A · chủ (host)", ["tính đường bóng", "gửi trạng thái 20 lần/giây"], "console", title_size=19)
    g.box(W - 348, 140, 300, 130, "Kit B · khách (guest)", ["vẽ sân lật ngược", "trả lời vị trí thanh đỡ"], "rs485", title_size=19)
    for y, name in ((175, "A"), (205, "B"), (235, "GND")):
        g.line([(348, y), (W - 348, y)], arrow=False, stroke=TEXT if name != "GND" else MUTED, width=2, dash=(name == "GND"))
        g.label(W / 2, y + 5, "%s nối %s" % (name, name), TEXT if name != "GND" else MUTED)
    g.text(W / 2, 300, "cổng J10 của mỗi kit · đổi chỗ A và B thì byte nhận về bị đảo bit", 14, MUTED, "middle")

    # time line: 0 .. 50 ms, 18 px per ms
    x0, y0, ms = 150, 380, 19.0
    g.text(48, 356, "Một chu kỳ 50 ms", 18, TEXT, weight=700)
    g.text(48, y0 + 28, "Kit A gửi", 15, colour("console"))
    g.text(48, y0 + 88, "Kit B gửi", 15, colour("rs485"))
    g.add('<rect x="%.1f" y="%d" width="%.1f" height="40" rx="4" fill="%s"/>' % (x0, y0, 11.5 * ms, colour("console")))
    g.text(x0 + 11.5 * ms / 2, y0 + 26, "S · 11 byte", 15, BG, "middle", 700, mono=True)
    g.add('<rect x="%.1f" y="%d" width="%.1f" height="40" rx="4" fill="%s"/>' % (x0 + 12.5 * ms, y0 + 60, 6.3 * ms, colour("rs485")))
    g.text(x0 + 12.5 * ms + 6.3 * ms / 2, y0 + 86, "P · 6 byte", 15, BG, "middle", 700, mono=True)
    g.add('<rect x="%.1f" y="%d" width="%.1f" height="100" rx="4" fill="none" stroke="%s" stroke-dasharray="6 5"/>'
          % (x0 + 19.5 * ms, y0, 30.5 * ms, LINE))
    g.text(x0 + 34.5 * ms, y0 + 56, "đường dây im lặng: khoảng 31 ms", 15, MUTED, "middle")
    g.line([(x0, y0 + 120), (x0 + 50 * ms + 20, y0 + 120)], stroke=MUTED)
    for t in (0, 10, 20, 30, 40, 50):
        g.line([(x0 + t * ms, y0 + 114), (x0 + t * ms, y0 + 126)], arrow=False)
        g.text(x0 + t * ms, y0 + 146, "%d ms" % t, 13, MUTED, "middle", mono=True)
    g.text(48, 566, "Khách chỉ nói ngay sau khi chủ nói xong, nên hai bên không bao giờ phát cùng lúc. Một byte ở 9600 baud dài khoảng 1 ms.", 15, MUTED)
    g.save(os.path.join(DOC_DIR, "diagram-pong.svg"))


if __name__ == "__main__":
    block_diagram()
    power_tree()
    layers()
    memory()
    ota()
    frame_loop()
    pong()
