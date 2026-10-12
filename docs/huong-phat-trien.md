# Hướng phát triển ak-mcu-base cho nhiều ứng dụng

Lập ngày 12/10/2026 từ một lượt tra cứu tài liệu công khai (14 lượt tìm, 3 trang nguồn đã mở). Mỗi mục ghi lợi
ích, chi phí, rủi ro và nguồn. Số liệu nào **chưa đối chiếu được với trang nguồn** hoặc chưa đo trên board thì ghi
"chưa kiểm chứng". Bổ sung cho [huong-toi-uu-tiep.md](huong-toi-uu-tiep.md) (tối ưu nền) và
[y-tuong-demo.md](y-tuong-demo.md) (demo trên kit); hai tài liệu đó không lặp lại ở đây.

Điểm xuất phát: base đã có kernel chạy trên ba nơi (chip, máy tính, trình duyệt), bootloader an toàn khi mất điện,
Modbus RTU slave/master kèm OTA, nhật ký sự cố, máy ảo bytecode cho lập trình kéo thả, và `tools/new_project.py`
để xuất dự án. Các hướng dưới đây chọn theo **giá trị trên công sức** và theo thứ tự nên làm.

Cỡ việc: S = vài giờ, M = một hai ngày, L = nhiều ngày.

## 1. Bốn hướng đáng làm trước

| # | Hướng | Cỡ | Mở ra ứng dụng gì |
|---|---|---|---|
| 1 | **Port chip thứ hai** (STM32G0 hoặc STM32C0) | M – L | Chứng minh lớp HAL đủ tổng quát; dùng base cho sản phẩm giá rẻ |
| 2 | **Modbus TCP qua W5500** (nanoMODBUS đã có transport TCP) | M | Gateway RTU ↔ TCP, thiết bị Ethernet công nghiệp |
| 3 | ~~**Xác nhận ảnh và quay lui**~~ (đã làm) | M | OTA hiện trường không cần kỹ thuật viên; điều kiện cần để nói "cập nhật an toàn" |
| 4 | ~~**Ghi log vào flash SPI**~~ (đã làm bước 1: vòng tròn) | S – M | Data logger, trạm thời tiết lưu 24 giờ, hộp đen sự cố |

Chi tiết từng hướng ở các mục dưới.

## 2. Nền và cập nhật firmware

### 2.1 Xác nhận ảnh và quay lui (confirm + rollback) — **đã làm** (bootloader 1.3.0, 12/10/2026)

Làm theo cách (a): vùng BACKUP 116K trên flash SPI ở `0xA0000`. Xem mục "Xác nhận và quay lui" trong
[ak-mcu-base.md](ak-mcu-base.md). Phần dưới giữ lại làm ghi chú thiết kế.

- **Là gì:** theo mô hình của MCUboot: ảnh mới chạy ở trạng thái "thử"; app tự kiểm (Modbus trả lời, kernel chạy đủ
  N giây) rồi mới đánh dấu "ổn". Reset mà chưa đánh dấu thì bootloader quay về ảnh cũ. Bộ đếm anti-rollback chỉ tăng
  **sau khi** ảnh được xác nhận, để vẫn quay lui được.
- **Base đã có:** `boot_ctrl` hai bản ghi, bộ đếm 3 lần thử, ảnh chờ ở flash SPI. Thiếu: nơi giữ ảnh cũ. Hai cách:
  (a) vùng staging thứ hai trên flash SPI (1 MB còn rộng: APP tối đa 116 K); (b) sao lưu APP ra SPI trước khi chép đè.
  Cách (a) đơn giản hơn và không tốn thời gian chép thêm lúc cài.
- **Rủi ro:** quay lui sai (ảnh mới tốt nhưng app quên xác nhận) → phải có lệnh shell `fw confirm` và test giả lập
  trên `ak_sim` như `test_sim_ota.py` đang làm với cắt điện.
- **Nguồn:** [MCUboot design](https://docs.mcuboot.com/design.html) (test swap, confirm, revert) ·
  thảo luận Nordic về chế độ một ô không quay lui được: [devzone](https://devzone.nordicsemi.com/f/nordic-q-a/127927/mcuboot-firmware-loader-ble_mcumgr-handling-of-two-slot-application/565567) ·
  bộ đếm bảo mật chỉ tăng sau xác nhận: [mcuboot commit](https://review.trustedfirmware.org/plugins/gitiles/mirror/mcuboot/+/0eaf6668189d9001ca5b94dba4e3d8051e7bbe83).

### 2.2 Ký số ảnh (ECDSA P-256) và Cyber Resilience Act

- **Vì sao bây giờ:** CRA của EU (Regulation 2024/2847) áp dụng nghĩa vụ báo cáo từ **11/09/2026** và áp dụng đầy đủ
  từ **11/12/2027**; sản phẩm thiết kế năm 2025 bán năm 2028 vẫn thuộc phạm vi. Quy định trung lập về công nghệ nhưng
  "cập nhật an toàn, kiểm được tính toàn vẹn bằng mật mã" là yêu cầu cốt lõi. Thiết bị **không có kết nối dữ liệu**
  có thể nằm ngoài phạm vi; thiết bị có OTA qua RS485/Ethernet thì không chắc.
- **Kích thước:** p256-m: mã ECDSA khoảng 2,9 – 3,0 KB, stack verify 700 – 752 B, không heap, **không gồm SHA-256**;
  thời gian verify 309 ms trên Cortex-M4 100 MHz và 1976 ms trên Cortex-M0 48 MHz. Trên Cortex-M3 32 MHz **chưa kiểm
  chứng**, ước 1 – 2 giây. Bootloader đang 8,2 K trên 12 K: thêm P-256 + SHA-256 (~1,5 – 2 KB) là sát nhưng có thể
  vừa; nếu không, nới BOOT lên 16 K (đổi địa chỉ link app: thiết bị đã bán phải nạp lại cả hai).
- **Việc khó thật sự:** quản lý khoá ký (khoá riêng trên máy ký, khoá công khai trong bootloader) và quy trình xoay
  khoá. Nên làm sau 2.1.
- **Nguồn:** [p256-m](https://github.com/mpg/p256-m) · [micro-ecc](https://github.com/kmackay/micro-ecc) ·
  CRA: [ac6: what changes for an embedded product](https://www.ac6-formation.com/en/articles/cyber-resilience-act-embedded-product) ·
  [Hubble: CRA deadline](https://hubble.com/community/guides/the-cra-deadline-is-100-days-out-here-s-what-your-ble-firmware-actually-has-to-do-to-comply/) ·
  [Keyfactor: secure firmware updates for CRA](https://docs.keyfactor.com/solution-areas/latest/implementing-secure-firmware-updates-for-cra) ·
  Memfault, "Secure firmware updates with code signing" (07/2025) và "Device firmware update cookbook" (03/2026) trên
  [interrupt.memfault.com](https://interrupt.memfault.com/) (chưa mở được trang từ môi trường này).

### 2.3 Cập nhật nén hoặc theo phần chênh (delta)

- **Là gì:** máy tính tạo bản vá bằng `detools` (nén heatshrink), thiết bị dựng lại ảnh mới từ ảnh cũ + bản vá.
  Espressif đóng gói đúng cách này thành `esp_delta_ota` (yêu cầu detools ≥ 0.49, heatshrink ISC, detools BSD-2).
  Giải mã heatshrink chạy ở chế độ ít RAM (window 8, lookahead 7) với bộ đệm tĩnh.
- **Đáng giá khi:** đường truyền là RS485 9600 baud (base đo 20 KB mất 26 giây; bản demo 57 KB hơn một phút).
- **Rủi ro:** bản delta chỉ khớp một phiên bản gốc; phải có vùng giữ ảnh cũ (trùng với 2.1) và kiểm CRC ảnh sau khi
  dựng (base đã có). Làm sau 2.1.
- **Nguồn:** [esp_delta_ota](https://components.espressif.com/components/espressif/esp_delta_ota) ·
  [esp32_compressed_delta_ota_update](https://github.com/ESP32-Musings/esp32_compressed_delta_ota_update/blob/master/README.md) ·
  [FreeRTOS: delta OTA với janpatch](https://shpp.freertos.org/2022/01/delta-over-the-air-updates.html).

### 2.4 Tiết kiệm điện: Stop mode đánh thức bằng RTC

- **Là gì:** kernel dùng timer mốc tuyệt đối nên "idle không tick" làm được thẳng: tính timer gần nhất, dừng SysTick,
  đặt RTC wakeup (EXTI), vào Stop, khôi phục HSE + PLL, cộng bù thời gian.
- **Mở ra:** cảm biến chạy pin (SHT45 + RS485 hỏi theo lịch, LoRa sau này). STM32L1 là dòng "L" đúng mục đích này.
- **Rủi ro:** RS485 slave ngủ sẽ mất khung đầu khi master hỏi; cần đánh thức theo cạnh RX và master thử lại. Số liệu
  0,8 – 1,5 µA của AN3193 **chưa kiểm chứng**; đầu dò debug cắm vào làm tăng dòng nên phải đo không có ST-Link.
- **Nguồn:** [ST: tips for low-power modes](https://community.st.com/t5/stm32-mcus/tips-for-using-stm32-low-power-modes/ta-p/621007) ·
  [ST: RTC wakeup from low power](https://community.st.com/t5/stm32-mcus/how-to-configure-the-rtc-to-wake-up-the-stm32-periodically-from/ta-p/49836) ·
  [FreeRTOS tickless trên STM32L](https://forums.freertos.org/t/use-tickless-low-power/3415) (cảnh báo dòng L khác dòng F).

## 3. Kết nối và ứng dụng

### 3.1 Modbus TCP qua W5500 — gateway RTU ↔ TCP

- **Vì sao hợp:** nanoMODBUS đã có `NMBS_TRANSPORT_TCP`; transport chỉ là hai hàm `read`/`write` có timeout (base
  đang cài cho RS485 ở `services/modbus/mb_port.c`). W5500 xử lý TCP/IP, ARP, ICMP trong phần cứng, 8 socket, 32 KB
  bộ đệm riêng, nối qua SPI: chip 16 K RAM không phải chạy stack TCP. Kit có cổng SPI mở rộng J6.
- **Việc:** driver W5500 tối thiểu (socket mở/đọc/ghi, ~300 dòng), `mb_port` thứ hai, task gateway chuyển khung
  TCP ↔ RTU. Lớp master RTU đã có.
- **Mở ra:** đọc biến tần, công tơ, cảm biến RS485 lên mạng LAN; thiết bị Ethernet công nghiệp giá rẻ; đây là loại
  dự án có nhu cầu thật (nhiều bài hướng dẫn STM32 + W5500 Modbus TCP, gateway RTU → TCP trên STM32F103).
- **Rủi ro:** SPI1 đang dùng chung flash NOR và nRF24; thêm W5500 phải chia sẻ bus (CS riêng) hoặc dùng SPI2.
- **Nguồn:** [nanoMODBUS](https://github.com/debevv/nanoMODBUS) · [W5500 Modbus TCP server trên STM32](https://maker.wiznet.io/sophia/projects/stm32-w5500-modbus-tcp-server-a-practical-ethernet-reference-for-industrial-devices/) ·
  [Gateway RTU → TCP trên STM32F103 + W5500](https://maker.wiznet.io/jakelee/resellers/modbus-rtu-to-tcp-gateway-with-w5500-and-stm32/) ·
  [nanoMODBUS + W5500 trên STM32F401](https://maker.wiznet.io/Lihan__/projects/w5500ethernet/).

### 3.2 Đọc biến tần mặt trời, công tơ, bộ sạc qua RS485

- **Là gì:** dùng Modbus master đã có để đọc bản đồ thanh ghi SunSpec (Fronius, SolarEdge) hoặc của hãng (Solax,
  Growatt, Deye, Sofar); hiện giá trị trên OLED, ghi log (mục 3.4), đẩy lên gateway (3.1).
- **Việc:** bảng thanh ghi dạng dữ liệu (`const` trong flash), lệnh shell `inv read`, màn hình demo. Nhiều dự án mã mở
  đã có sẵn bản đồ thanh ghi để tham khảo (SolaxModbusGateway cho ESP32 có Solax, Growatt, Deye, Sofar).
- **Rủi ro:** cần thiết bị thật để thử; không có thì thử với `tools/ak_mb.py` đóng vai slave giả.
- **Nguồn:** [SunSpec Modbus RTU của Fronius](https://www.fronius.com/en-gb/uk/solar-energy/installers-partners/technical-data/all-products/system-monitoring/open-interfaces/modbus-rtu) ·
  [SolaxModbusGateway](https://github.com/tobiasfaust/SolaxModbusGateway) ·
  [read_solaredge_modbus](https://github.com/e-cite/read_solaredge_modbus).

### 3.3 Console qua USB (CDC) không cần CH340

- **Là gì:** STM32L151 có USB FS; kit nối USB-C qua CH340E. Một stack CDC nhỏ cho phép bỏ IC chuyển đổi trên sản
  phẩm xuất ra từ base.
- **Thực tế:** TinyUSB liệt kê STM32 **L1** trong bản README mới (bản cũ không có) — **phải kiểm trong repo chính
  trước khi tin**. libopencm3 có `lib/stm32/l1` nhưng trang danh sách không hiện file USB. Không tìm thấy ví dụ CDC
  bare-metal không HAL riêng cho L151; eddyem/stm32samples có CDC cho F0/F1/F3/F4/G0.
- **Chi phí:** TinyUSB device + CDC cỡ 6 – 10 KB flash (ước, **chưa kiểm chứng**), RAM vài trăm byte; cần vòng
  `tud_task()` trong polling của kernel.
- **Nguồn:** [TinyUSB README (bản ghi có L1)](https://pigweed.googlesource.com/third_party/github/hathach/tinyusb/+/8fe9022a6e6dea785092f4e0dff8180f1f0d4d86/README.rst) ·
  [eddyem/stm32samples](https://git.sao.ru/eddy/stm32samples/commit/21141c29b23288779772717bfa04b3ac476a89a1) ·
  [libopencm3 lib/stm32/l1](https://github.com/libopencm3/libopencm3/tree/master/lib/stm32/l1).

### 3.4 Ghi log vào flash SPI: vòng tròn, rồi littlefs

- **Bước 1 (S):** vòng ghi bản ghi cố định (thời gian, nhiệt độ, độ ẩm, điện áp) trên vài sector flash SPI, tìm đầu
  cuối bằng số thứ tự như `crash_log` đang làm với EEPROM; lệnh `log csv`. Trạm thời tiết hiện giữ 96 điểm trong RAM.
- **Bước 2 (M):** littlefs khi cần nhiều file (cấu hình, bài hát, clip video, chương trình kéo thả). littlefs chịu
  mất điện, cân bằng mòn, RAM cố định không phụ thuộc kích thước hệ thống file; Mbed đo ít hơn FAT ~13 KB mã và 4 KB
  RAM. Thiết kế cho chip ~32 KiB RAM nên trên 16 K phải cấu hình bộ đệm nhỏ (`cache_size`, `lookahead_size` 16 – 64 B).
  SPIFFS không còn được bảo trì, không chọn.
- **Nguồn:** [littlefs DESIGN](https://codex.ac6.fr/Zephyr/raw/littlefs-latest/DESIGN.md) ·
  [Espressif: file system considerations](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/file-system-considerations.html) ·
  [cnx-software: littlefs](https://www.cnx-software.com/2018/01/02/littlefs-is-an-open-source-low-footprint-resilient-file-system-designed-for-tiny-devices/).

### 3.5 Port chip thứ hai

| Chip | Vì sao | Lưu ý |
|---|---|---|
| **STM32G0** (Cortex-M0+, 64 MHz) | Được đánh giá là "mặc định tốt cho sản xuất": giá, hệ sinh thái, nguồn cung lâu dài; cùng SPL-style/CMSIS, flash ghi theo double-word | Port gần nhất với L151: `port_cfg.h`, `startup.c`, `hal_flash`, linker |
| **STM32C0** (Cortex-M0+, 48 MHz, 32 K) | ST nhắm thay 8 bit; rẻ nhất trong họ STM32 | 32 K flash: chỉ bootloader + app Modbus, không demo; staging phải ở SPI ngoài |
| **RP2040 / RP2350** | 1 USD, hai nhân, cộng đồng lớn; RP2350 có secure boot | Flash ngoài qua XIP: bootloader/OTA khác hẳn (phải chạy từ RAM khi ghi flash) |
| **CH32V003** (RISC-V, 2 K RAM, 16 K flash) | Dưới 0,10 USD | Quá nhỏ cho kernel + bootloader; toolchain chưa chín |

Khuyến nghị: **STM32G0** trước (porting.md đã liệt kê đúng 6 file phải viết). Wokwi mô phỏng STM32C031 và
STM32L031 nên bản C0 có thể chạy thử trong trình duyệt của Wokwi; Renode có `stm32g0.repl` nhưng **không có L1**.

- **Nguồn:** [Top 5 MCU 2026 (Predictable Designs)](https://predictabledesigns.com/top-5-microcontrollers-in-2026-best-performance-for-the-price/) ·
  [STM32C0 (cnx-software)](https://www.cnx-software.com/2023/01/13/low-cost-stm32c0-32-bit-microcontroller-aims-to-displace-8-bit-mcus/) ·
  [RP2040 (Wikipedia)](https://en.wikipedia.org/wiki/RP2040) · [CH32V003](https://gittrend.io/repo/openwch/ch32v003) ·
  [Renode platforms/cpus](https://github.com/renode/renode/tree/master/platforms/cpus) ·
  [Wokwi STM32 hello](https://github.com/wokwi/stm32-hello-wokwi).

### 3.6 Lập trình kéo thả: học theo MicroBlocks

- **Đã có:** máy ảo `blk_vm.c`, trang Blockly, nạp qua Web Serial, bảy ví dụ.
- **MicroBlocks** là chuẩn tham chiếu cho giáo dục: máy ảo stack, lệnh 32 bit (8 bit opcode + 24 bit toán hạng),
  chạy trên chip có **16 KB RAM**, hỗ trợ "live coding" (sửa khối là chạy ngay, không nạp lại) và chương trình tự chạy
  khi rút cáp. Hàng nghìn người dùng, phần lớn là người mới.
- **Lấy gì:** (a) đồng bộ từng khối thay vì nạp cả ảnh → cảm giác "live"; (b) nhiều script chạy song song (mỗi
  script là một task kernel — base làm tự nhiên); (c) bộ ví dụ có bài học. Không cần đổi máy ảo.
- **Mở ra:** kit dạy học cho trường/câu lạc bộ, dùng chính kernel và bootloader thật thay vì Arduino.
- **Nguồn:** [MicroBlocks VM](https://wiki.microblocks.fun/virtual_machine) · [smallvm](https://github.com/beagleboard/smallvm) ·
  [Make: Microcontrollers, Meet MicroBlocks](https://makezine.com/article/technology/iot/microcontrollers-meet-microblocks/).

### 3.7 Không nên: MicroPython trên L151

Bản tối thiểu của MicroPython dưới 80 K mã Thumb2, cần 8 K heap để chạy script đơn giản; trên 128 K flash chỉ còn chỗ
cho vài driver, và 16 K RAM trừ kernel, Modbus, màn hình thì heap không đủ. Người port lên STM32F103 64 K không xuống
được 70 K mà còn REPL. Máy ảo bytecode hiện có là lựa chọn đúng cho chip này.
Nguồn: [MicroPython FAQ](https://github.com/micropython/micropython/wiki/FAQ) ·
[forum: port BluePill](https://forum.micropython.org/viewtopic.php?p=21957).

## 4. Kiểm thử và công cụ

### 4.1 Fuzz các bộ phân tích khung trên máy tính (S)

Base đã build toàn bộ mã trên host với ASan + UBSan: chỉ thiếu harness. Hai bộ phân tích đáng fuzz: `fw_proto`
(khung nạp firmware, header ảnh) và `mb_slave`/`mb_ota` (khung Modbus). libFuzzer với `LLVMFuzzerTestOneInput` gọi
thẳng `fw_proto_rx_byte()` / `mb_slave_poll()` với dữ liệu giả; chạy vài phút trong CI. Hướng dẫn thực hành khuyên
bắt đầu đúng từ bộ phân tích header OTA.
Nguồn: [Beningo: fuzzing for embedded](https://www.beningo.com/embedded-technology-atlas/2026/pages/024-fuzzing-for-embedded.html) ·
[ví dụ harness libFuzzer (usbredir)](https://gitlab.freedesktop.org/spice/usbredir/-/merge_requests/6).

### 4.2 Chạy ảnh thật trong Renode (M, chỉ sau khi có port G0)

Renode chạy đúng file `.elf` của chip trong CI (GitHub Actions + Robot Framework), bắt được lỗi khởi động và linker
mà test host không thấy. **Không có platform STM32L1** trong Renode; có `stm32f103.repl`, `stm32g0.repl`, `stm32f4.repl`.
Vậy hướng này đi cùng port STM32G0. Với L151, `ak_sim` và bản web vẫn là cách chạy trên máy tính.
Nguồn: [Memfault: Renode + GitHub Actions](https://interrupt.memfault.com/blog/test-automation-renode) ·
[Hubble: CI/CD cho embedded](https://hubble.com/community/guides/how-to-set-up-ci-cd-for-embedded-projects/) ·
[Antmicro: ChromiumOS EC trong Renode](https://antmicro.com/blog/2023/06/chromiumos-ec-testing-suite-in-renode-for-consumer-grade-products).

### 4.3 Vết nhị phân thay cho log chuỗi (M)

Đã nêu ở huong-toi-uu-tiep.md mục 6; nhắc lại vì nó là điều kiện để làm 2.4 (log chuỗi qua UART không hợp với
thiết bị ngủ) và để test task bằng cách bơm message rồi đối chiếu vết.

## 5. Thứ tự đề xuất

1. **Log vào flash SPI (3.4 bước 1)** — nhỏ, dùng ngay cho trạm thời tiết và hộp đen sự cố.
2. **Confirm + rollback (2.1)** — đóng rủi ro lớn nhất của OTA; mọi ứng dụng hiện trường đều cần.
3. **Modbus TCP qua W5500 (3.1)** — ứng dụng có nhu cầu thật, tận dụng Modbus master/slave sẵn có.
4. **Port STM32G0 (3.5)** rồi **Renode trong CI (4.2)**.
5. **Fuzz (4.1)** — rẻ, làm xen kẽ bất cứ lúc nào.
6. **Ký số ảnh (2.2)** — khi có sản phẩm bán vào EU hoặc khách hàng hỏi về CRA.
7. **Stop mode (2.4)**, **delta OTA (2.3)**, **USB CDC (3.3)** — theo nhu cầu của sản phẩm cụ thể.

## 6. Nguồn không mở được từ môi trường tra cứu

`interrupt.memfault.com` và `docs.tinyusb.org` không phân giải được tên miền khi lập tài liệu này; các mục dẫn tới
hai trang đó lấy từ kết quả tìm kiếm và bản sao (pigweed mirror), **chưa đọc trang gốc**.
