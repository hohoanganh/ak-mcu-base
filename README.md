# ak-mcu-base — Firmware source base cho STM32L151

**Tiếng Việt** · [English](README.en.md)

<p align="center">
  <a href="https://hohoanganh.github.io/ak-mcu-base/play/"><img src="docs/demo-highlights.gif" alt="Demo trên AK Base Kit: logo xoay 3D, khối 3D, mê cung, Tetris, Dino, Invaders, máy hiện sóng" width="404"></a>
</p>

<p align="center">
  <a href="https://hohoanganh.github.io/ak-mcu-base/play/"><img src="https://img.shields.io/badge/%E2%96%B6%20CH%E1%BA%A0Y%20TH%E1%BB%AC%20NGAY-tr%C3%AAn%20tr%C3%ACnh%20duy%E1%BB%87t-a8ff3e?style=for-the-badge&labelColor=0b0f14" alt="Chạy thử firmware ngay trên trình duyệt"></a>
  <a href="https://hohoanganh.github.io/ak-mcu-base/play/pong.html"><img src="https://img.shields.io/badge/HAI%20KIT-ch%C6%A1i%20Pong%20qua%20RS485-d9e1ea?style=for-the-badge&labelColor=0b0f14" alt="Hai kit chơi Pong qua RS485"></a>
</p>

<p align="center">
  <a href="https://github.com/hohoanganh/ak-mcu-base/actions/workflows/ak-mcu-base.yml"><img src="https://github.com/hohoanganh/ak-mcu-base/actions/workflows/ak-mcu-base.yml/badge.svg" alt="CI"></a>
  <img src="https://hits.sh/github.com/hohoanganh/ak-mcu-base.svg?style=flat-square&label=Repo%20Traffic&color=007ec6" alt="Repo Traffic">
</p>

Nền firmware bare-metal cho **STM32L151CBT6** (AK Base Kit): kernel AK hướng sự kiện không cần RTOS, bootloader,
cập nhật firmware qua UART và RS485. Dựng lại từ
[ak-base-kit-stm32l151](https://github.com/the-ak-foundation/ak-base-kit-stm32l151) của AK Foundation.

Ảnh động ở trên **không phải ảnh chụp**: nó được dựng từ chính mã vẽ của firmware. Và hai nút bên dưới nó chạy đúng mã C
đó trong trình duyệt, không cần kit.

| Muốn… | Vào đây |
|---|---|
| **Xem nó chạy** | [Một kit, đủ 17 màn hình](https://hohoanganh.github.io/ak-mcu-base/play/) · [hai kit chơi Pong với nhau](https://hohoanganh.github.io/ak-mcu-base/play/pong.html) |
| **Lập trình bằng kéo thả** | [Nối các khối lại, bấm Chạy](https://hohoanganh.github.io/ak-mcu-base/play/blocks.html): chạy ngay trên kit trong trang, gửi được sang kit thật qua USB |
| **Biết demo có gì** | [Từng màn hình, kèm ảnh động](docs/demo-kit.md) |
| **Bắt đầu dự án mới** | `python tools/new_project.py <thư-mục> --board <tên-board>` · [hướng dẫn](docs/tien-ich.md) |
| **Tải firmware** | [Releases](https://github.com/hohoanganh/ak-mcu-base/releases) |
| **Đọc tổng quan** | [Trang giới thiệu](https://hohoanganh.github.io/ak-mcu-base/) |

## Phần cứng: AK Base Kit

<p align="center">
  <img src="docs/kit/ak-base-kit-v3.jpg" alt="AK Base Kit bản 3: tấm mica màu hổ phách phía trên, màn hình OLED, ba nút bấm tròn, nút reset xanh, ba cổng nối ở cạnh" width="49%">
  <img src="docs/kit/board-view-top.png" alt="Sơ đồ mặt trên của bo: màn hình ở giữa, ba nút S1 S2 S3 phía dưới, các hàng chân RS485, UART3, I2C bên phải, SWD và console bên trái" width="49%">
</p>

<p align="center">
  <a href="https://hohoanganh.github.io/ak-mcu-base/kit/ak-base-kit-3i0-3d.html"><img src="docs/kit/ak-base-kit-3i0-render-top.jpg" alt="Bản dựng 3D mặt trên của AK Base Kit 3.0: màn hình OLED, bốn nút bấm, các hàng chân có tên" width="49%"></a>
  <a href="https://hohoanganh.github.io/ak-mcu-base/kit/ak-base-kit-3i0-3d.html"><img src="docs/kit/ak-base-kit-3i0-render-bottom.jpg" alt="Bản dựng 3D mặt dưới của AK Base Kit 3.0: vi điều khiển, pin đồng hồ, còi, các cổng nối, USB Type-C" width="49%"></a>
</p>

<p align="center"><b><a href="https://hohoanganh.github.io/ak-mcu-base/kit/ak-base-kit-3i0-3d.html">Xem mô hình 3D của bo trên trình duyệt →</a></b><br>
xoay, phóng to, và nhấn vào từng linh kiện để xem nó là gì (118 linh kiện, có ô tìm)</p>

STM32L151CBT6, màn hình OLED 1,54 inch 128×64, ba nút bấm, còi, RTC có pin, flash SPI 1 MB, RS485, UART, I2C,
USB Type-C có sẵn mạch USB–UART. Hai ảnh trên là bản dựng từ file thiết kế bo 3.0, không phải ảnh chụp. Ảnh và sơ đồ bo: [AK Foundation](https://github.com/the-ak-foundation/ak-base-kit-stm32l151/tree/main/hardware/images) (giấy phép MIT).

### Trên bo có gì

<p align="center">
  <img src="docs/kit/block-diagram.svg" alt="Sơ đồ khối của AK Base Kit: STM32L151CBT6 ở giữa; USB Type-C, CH340E, cổng nạp SWD và thạch anh bên trái; OLED, flash SPI, RTC, RS485 bên phải; các cổng mở rộng phía trên; nút bấm, còi, LED phía dưới" width="860">
</p>

### Chân nào làm việc gì

<p align="center">
  <img src="docs/kit/stm32l151-pinout.svg" alt="Sơ đồ chân STM32L151CBT6 trên AK Base Kit: console UART1 ở PA9 PA10, RS485 ở PA1 PA2 PA3, flash SPI ở PA5 PA6 PA7 PB14, OLED ở PB12 PB13 PA15, I2C ở PB6 PB7, ba nút ở PB3 PC13 PB4, còi PB0, LED PB8, SWD ở PA13 PA14" width="860">
</p>

Sơ đồ đã đối chiếu với schematic AK MCU KIT 3.0 và khớp với cấu hình chân trong firmware
(`src/port/stm32l151`). Sinh bởi [`tools/pinout_svg.py`](tools/pinout_svg.py).

### Các cổng nối

<p align="center">
  <img src="docs/kit/connectors.svg" alt="Mười cổng nối của AK Base Kit 3.0 và tín hiệu trên từng chân: J14 nạp và console, J15 SWD, J12 console UART1, J10 RS485, J4 UART3, J7 I2C1, J13 chân vi điều khiển, J6 SPI mở rộng, J9 I2C và UART mở rộng, J3 màn hình OLED" width="860">
</p>

Theo schematic AK MCU KIT 3.0, sinh bởi [`tools/connectors_svg.py`](tools/connectors_svg.py).
Bảng chân dạng chữ: [demo-kit.md](docs/demo-kit.md#phần-cứng-ak-base-kit-3i0).

### Cây nguồn

<p align="center">
  <img src="docs/kit/power-tree.svg" alt="Cây nguồn của bo: USB Type-C 5V qua cầu chì và diode; LDO RT9013 tạo 3V3 cho vi điều khiển và các IC; mạch tăng áp TPS61040 tạo 12,5V cho OLED; còi và nguồn ra cổng nối dùng 5V" width="860">
</p>

## Phần mềm: một mã nguồn, ba nơi chạy

<p align="center">
  <img src="docs/diagram-layers.svg" alt="Các lớp của ak-mcu-base: ứng dụng, kernel và dịch vụ, HAL; bên dưới là ba port: chip STM32L151, máy tính chạy unit test, trình duyệt chạy WebAssembly" width="860">
</p>

Sơ đồ bộ nhớ, luồng cập nhật firmware và cách demo chạy trên kernel: xem [docs/ak-mcu-base.md](docs/ak-mcu-base.md)
và [tài liệu demo](docs/demo-kit.md).

## Bắt đầu nhanh

Cần PlatformIO và ST-Link (hoặc chỉ một máy tính, nếu chỉ chạy test).

```bash
pio run -e boot -t upload      # 1. bootloader, 0x08000000
pio run -e demo -t upload      # 2. demo trên AK Base Kit (hoặc -e app: app mẫu, Modbus slave)
pio device monitor             # console UART1, 115200: gõ help
```

Từ lần sau cập nhật qua cổng console, không cần ST-Link: `python tools/ak_fw.py --port COMx flash .pio/build/demo/app.img`.

Chạy test và bộ giả lập trên máy tính (Linux / WSL): `make test`, `make sim`.
Dự án mới: `python tools/new_project.py <thư-mục> --board <tên-board>` xuất một dự án độc lập, không dính repo này.
File `.img` và `.bin` của mọi bản: [Releases](https://github.com/hohoanganh/ak-mcu-base/releases).

## Tài liệu

| Việc | Đọc |
|---|---|
| Kiến trúc, bản đồ flash, bootloader và OTA, đã kiểm gì | [docs/ak-mcu-base.md](docs/ak-mcu-base.md) |
| Demo trên kit: 17 màn hình, lập trình kéo thả, phần cứng, cách viết màn hình mới | [docs/demo-kit.md](docs/demo-kit.md) |
| Nhật ký sự cố, giám sát task, đo stack, tạo dự án mới, CI | [docs/tien-ich.md](docs/tien-ich.md) |
| Modbus RTU trên RS485: slave, master, OTA | [docs/modbus.md](docs/modbus.md) |
| Port sang chip khác | [docs/porting.md](docs/porting.md) |
| Biết bản nào sửa lỗi gì | [CHANGELOG.md](CHANGELOG.md) |
| Cho AI assistant tra tài liệu kernel AK | [docs/ak-mcp-docs-server.md](docs/ak-mcp-docs-server.md) — repo có sẵn [.mcp.json](.mcp.json) |

## Cấu trúc thư mục

```
ak-mcu-base/
├── src/            mã nguồn firmware
│   ├── kernel/     kernel AK: task, message, timer, fsm, tsm
│   ├── hal/        giao diện phần cứng mà mọi port phải có
│   ├── common/     xprintf, CRC, log
│   ├── services/   fw (ảnh, OTA, boot_ctrl) · sys (nhật ký sự cố) · modbus
│   ├── boot/       bootloader, không phụ thuộc chip
│   ├── app/        app mẫu: watchdog và giám sát task, shell, OTA  ← dự án viết task ở đây
│   ├── demo/       demo trên AK Base Kit (OLED, 3 nút, còi)
│   └── port/       stm32l151 (chip) · host (máy tính, ak_sim) · web (WebAssembly)
├── tests/          unit test và test đầu-cuối trên ak_sim
├── tools/          ak_fw.py, ak_mb.py, mkimage.py, new_project.py, tool cho demo và sơ đồ
├── third_party/    nanoMODBUS · SPL + CMSIS của STM32L1 mà bản build dùng
├── docs/           tài liệu và trang web (GitHub Pages)
├── legacy/         base cũ (v1.3.0), chỉ còn bảo trì
├── platformio.ini  build bằng PlatformIO: env boot, app, app_mbmaster, demo
└── CMakeLists.txt  build bằng CMake, test trên máy tính
```

Trong tài liệu và chú thích mã, đường dẫn kiểu `port/stm32l151`, `demo/scr_pong.c`, `services/fw` là tính **từ `src/`**.

## Base cũ nằm ở `legacy/`

Đến 07/10/2026 repo này tên là `ak-base-kit-pio` và có hai base nằm cạnh nhau: base cũ ở gốc repo (thư mục `sources/`) và base mới trong
`ak-mcu-base/`. Nay **base mới ở gốc repo**, base cũ chuyển nguyên vẹn vào [`legacy/`](legacy/README.md) cho các dự án
đang chạy trên nó, và repo đổi tên thành `ak-mcu-base` (GitHub tự chuyển hướng địa chỉ cũ; riêng trang web chuyển sang
`hohoanganh.github.io/ak-mcu-base`, địa chỉ cũ không còn). Các tag cũ (`v1.0.0` … `v1.3.0`, `ak-mcu-base-v1.x`) vẫn giữ cấu trúc thư mục của thời điểm đó.

| | Base này (gốc repo) | [`legacy/`](legacy/README.md) — base cũ |
|---|---|---|
| Phiên bản | **v1.3.7**, bootloader 1.2.1 | **v1.3.0**, bootloader 0.0.3 |
| Dùng khi | Dự án mới: OTA an toàn khi mất điện, có unit test, dễ chuyển sang chip khác | Dự án đang chạy trên nó; cần driver có sẵn (EEPROM ngoài, nRF24, lớp Arduino…) |
| Mức hoàn thiện | Kernel, bootloader, OTA, Modbus RTU, nhật ký sự cố, demo trên kit; driver khác tự viết thêm | Đầy đủ, đã dùng cho sản phẩm |
| Build | PlatformIO hoặc CMake | PlatformIO (`cd legacy`) |
| Cập nhật firmware | UART và RS485 (Modbus); ảnh có CRC32, tên board, chống cài dở | UART, RS485 (Modbus), flash SPI ngoài |
| Kiểm thử trên máy tính | Kernel, bootloader, Modbus, OTA đầu-cuối trên giả lập, màn hình demo | Lớp Modbus |

So sánh chi tiết, kèm những gì base mới còn thiếu: **[xem trang so sánh](https://hohoanganh.github.io/ak-mcu-base/ak-mcu-base-so-voi-base-cu.html)** · bản Markdown: [docs/so-voi-base-cu.md](docs/so-voi-base-cu.md).

## Nguồn gốc

- Base: [the-ak-foundation/ak-base-kit-stm32l151](https://github.com/the-ak-foundation/ak-base-kit-stm32l151)
  (AK Embedded Base Kit, GaoKong), bản build bằng Makefile.
- Mô hình PlatformIO (build ngoài repo, script cờ biên dịch, tự chép release) rút từ một dự án sản phẩm
  trước đó, không công khai.
