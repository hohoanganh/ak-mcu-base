# Lịch sử thay đổi

Dự án mới luôn clone theo **tag mới nhất** và ghi version base vào README của mình — đó là cách duy
nhất biết sau này dự án đang thiếu bản sửa nào. Bootloader có số version riêng (`BOOT_VER`, in ra
console lúc khởi động).

Tổng hợp tính năng v1.2.0 – v1.3.0 (Modbus mới + OTA qua RS485):
[docs/tinh-nang-moi-v1.2-v1.3.md](legacy/docs/tinh-nang-moi-v1.2-v1.3.md).

## Chưa phát hành

- **Tách khung `fw_proto` bằng Rust (`AK_RUST_PROTO` / PlatformIO `rust_proto`, mặc định tắt).** App gọi
  `ak_rs_proto_feed()` của `libak_ffi.a` (repo `hohoanganh/ak-mcu-rust`, crate `ak-ffi`) thay máy trạng thái C; phần sau
  khung vẫn là C, bootloader giữ bản C. Rust panic → `FATAL("RS", 1)`. Demo +312 B flash, +272 B RAM (`libc` link trước
  `libak_ffi`, `__aeabi_memcpy*` trỏ về newlib — không thì +2,9 KB vì Rust mang `memcpy` riêng). Trên kit: nạp qua COM,
  shell vẫn nhận lệnh, OTA 58 KB qua bộ tách khung Rust. CI job `rust-proto`: `make test` 13/13 và demo STM32 với Rust.

## ak-mcu-base v1.3.11 — 30/10/2026

App **1.3.11**, bootloader **1.4.0**. App mặc định dùng RAM 32K nên **phải nạp bootloader 1.4.0 bằng ST-Link trước**:
bootloader 1.3.0 thấy stack của app ở `0x20008000` là vector hỏng, cài lại bản BACKUP (đã thử trên kit: không treo).

- **RAM 32K (`ram_size_kb` / `AK_RAM_SIZE`, mặc định 32).** Chip STM32L151CB của kit là loại Cat.2 (DEV_ID `0x429`),
  có 32K RAM; base trước đây khai 16K, con số sót lại từ kit 2.1 (L151C8T6) khi nâng cấp chip. Chip Cat.1 (`0x416`,
  16K) thì build `ram_size_kb = 16`. Bootloader luôn link 16K nên khởi
  động trên mọi chip; build 32K thì nó nhận app có stack tới `0x20008000`, nhưng từ chối nếu chip báo Cat.1 (thay vì
  để app treo cứng ngay lần push đầu tiên). Lệnh `info` in `chip id`: số `0x000` nghĩa là không đọc được mã chip khi
  không có debugger (errata của nhiều dòng STM32) — khi đó bootloader tin giá trị lúc build. Bootloader và app phải
  build cùng giá trị. Đã build 10 bản PlatformIO (16K) và boot + demo ở 32K (`_estack` `0x20008000`), `make test`
  13/13. **Trên kit (30/10/2026):** ghi/đọc qua SWD `0x20003FF0` và `0x20007FF0` giữ hai giá trị khác nhau (32K thật,
  không lặp địa chỉ); bản demo 32K: stack chưa dùng 24 408 B (16K: 8 024 B), chạy khối 3D + mê cung + tự chơi 60 s
  không sự cố; `info` in `chip id 0x429`. Bootloader 16K + app 32K: `app: bad vector table` → tự quay về bản BACKUP,
  không treo. Gỡ dây CLK/DIO rồi tắt bật nguồn: `reset reason 1`, `chip id 0x429` — firmware tự đọc được mã chip khi
  không có debugger, nên chốt chặn "chip Cat.1 thì từ chối app 32K" dùng được.

- **App Android nạp firmware qua USB OTG (`tools/android/`).** Cắm bộ USB-UART (CH340, CP210x, FTDI, PL2303, cổng COM
  ảo của ST-LINK/V2-1) vào điện thoại: xem thông tin board, nạp `.img`, xác nhận, quay lui, vào bootloader, reset, và gõ
  lệnh console. Giao thức `fw_proto` chuyển từ `tools/ak_fw.py` sang Kotlin thuần (`proto/`), có các ca của
  `tests/test_ak_fw.py` và một test nạp đầu-cuối trên `ak_sim`; job CI `android` chạy test đó và build APK. **Phần giao
  thức đã qua test trên `ak_sim`; app chưa build trong môi trường này (không tải được Android SDK), chưa chạy trên
  điện thoại và kit.**
- **Đo khả năng làm bộ điều khiển bay (P0 của `docs/drone-ak-mcu.md`).** Lõi điều khiển `src/fc/fc_ctrl.c` (lọc gyro,
  Mahony, PID góc và tốc độ góc, trộn QUADX; bản float và Q16, không libm), test `tests/test_fc.c` (9 ca). Lệnh shell
  `fcb` trong bản `kit_tools` (`port/stm32l151/fc_bench.c`): chu kỳ CPU của một bước, đọc IMU trên I2C1, độ đều của ngắt
  TIM6 1 kHz khi demo đang chạy. `kit_i2c_write()` mới trong `kit.c`; vector TIM6 trong `startup.c`. Bản `kit_tools`
  +6,7 KB flash, RAM còn 3.616 B cho heap + stack. **Chưa chạy trên kit.**
- **Tay điều khiển và bên thu (P1).** Bản `rc_tx` và `rc_rx` (`pio run -e rc_tx|rc_rx`, `make stm32-rc-tx|rc-rx`, CMake
  `AK_APP=rc_tx|rc_rx`), kit 3.0 + nRF24L01+ ở J6. `src/rc/rc_link.c`: gói có CRC16, nhảy 8 kênh theo ID, bên thu bám theo
  chỉ số kênh trong gói, arm/failsafe 200 ms, hiệu chỉnh cần gạt, bản ghi EEPROM `[224,256)`; `tests/test_rc.c` mô phỏng
  hai đầu qua mất gói, mất sóng, gửi trễ (chi tiết trong `docs/drone-ak-mcu.md` 6.2). `port/stm32l151/nrf24.c`: Enhanced
  ShockBurst với telemetry trong ACK. Cần gạt từ ADS1115 trên J7 hoặc lệnh `link stick` (`tools/rc_pc_sticks.py`).
  `gfx_flush_step()` đẩy OLED từng mẩu nhỏ; `kit_i2c_write_n()`. **Chưa chạy trên kit; bản PlatformIO chưa build được
  trong môi trường này (CMake build sạch).**
- **FC trên bàn (P2).** Bản `fc` (`pio run -e fc`, `make stm32-fc`, `AK_APP=fc`): `rc_rx` + ngắt TIM6 500 Hz ưu tiên 0 đọc
  MPU-6050 (I2C1), chạy `src/fc/fc_loop.c` (xoay trục IMU, hiệu chỉnh gyro lúc đứng yên, cổng an toàn arm/gói cuối
  < 200 ms/IMU/gyro ngay trong ngắt, giới hạn công suất), xuất PWM 16 kHz ở PA0, PB10, PB11, PB9 (`port/stm32l151/fc_hw.c`).
  Lệnh `fc` (trạng thái, thử từng động cơ, giới hạn, hiệu chỉnh lại). `tests/test_fc_sim.c` bay vòng điều khiển trong một
  quad 65 mm mô phỏng ở 1 kHz và 500 Hz. `FC_LOOP_HZ` cấu hình được, `fc_f_reset_pid()`. **Chưa chạy trên kit.**

## ak-mcu-base v1.3.10 — 16/10/2026

App **1.3.10**, bootloader **1.3.0** (không đổi). Nâng cấp từ v1.3.9 qua OTA (UART hoặc RS485) được, không cần ST-Link.

- **Bootloader 1.4.0: loader trên đường thứ hai (`BOOT_AUX_LOADER`, mặc định TẮT)** — 15/10/2026.
  - **Cách chạy:** bật cờ thì sau mỗi lần reset bootloader nghe `hal_rs485` trong `BOOT_AUX_WINDOW_MS` (300 ms). Một khung `LOADER` (06)
    tới trong cửa sổ thì bootloader ở lại chế độ nạp **trên chính đường đó**.
  - **Để làm gì:** máy chủ nối ở đường đó (ví dụ gateway trên USART2) nạp lại được app hỏng, không cần console hay bootloader
    ROM. Trên STM32L151xB-A vỏ 48 chân, bootloader ROM chỉ nghe USART1: USART2 của nó nằm ở PD5/PD6, chân vỏ này không có.
  - **Khung khác trong cửa sổ** (`INFO` của OTA đang chờ cài): được trả lời, không đổi gì.
  - **Rảnh `BOOT_AUX_IDLE_MS`** (60 s) không có khung thì rời chế độ nạp.
  - **Board bật cờ** phải link `hal_rs485`. Tắt cờ thì build y như cũ.
  - **Đổi chung cho mọi board:** `loader_loop()` nhận đường giao tiếp qua `loader_io_t`. Số version bootloader lên 1.4.0.
  - **Bài thử:** `tests/test_boot_aux.c` (target `test_boot_aux`, ctest `boot_aux`, 25 phép kiểm). Port host thêm
    `host_rs485_keep_rx_on_init()`. `make test` 13/13 (sau khi gộp với main ngày 28/10).
  - **Chưa thử trên chip** ở repo base.
- **Trang "Bên trong firmware"** (`docs/play/inside.html`): cho người không lập trình xem mã nguồn chạy thế nào, trên chính
  firmware chạy trong trang. (1) Bốn việc của vòng lặp chính, kèm đoạn mã thật của `kernel/src/task.c`. (2) **Mọi message
  kernel giao cho task**, đọc thật từ firmware: ai gửi, ai nhận, tín hiệu gì, lời giải thích và file xử lý; dừng firmware
  lại và chạy từng message một được. (3) Bảy bước từ lúc cấp điện, có nút khởi động lại kit để xem các message đầu tiên.
  (4) Nạp firmware có sao lưu, chạy thử, quay lui: mô phỏng trên trang, rút điện được ở từng bước. (5) Thư mục nào làm gì.
- Bản web bật `AK_TASK_TRACE_ENABLE` (móc theo dõi có sẵn của kernel) và thêm `web_trace()`, `web_step()`,
  `web_trace_names()` trong `port/web/web_main.c`; `kit-ui.js` thêm `pause()`, `step()`, `trace()`, `names()`. Firmware
  cho kit không đổi. Trong bản web thời gian là ảo nên thời gian chạy của từng handler luôn là 0 và trang không hiện nó.
- **OLED chỉ gửi đoạn cột đã đổi.** `kit_lcd_write(page, x, data, n)` thay `kit_lcd_write_page()` (port STM32, web,
  `test_demo`); `gfx_flush()` gửi trong mỗi trang đổi từ cột đổi đầu tới cột đổi cuối, `gfx_flush_bytes()`, lệnh `ui`
  in số byte. Tính trên khung của `test_demo`: thời gian bus giảm 36 – 91 % tuỳ màn hình (pong 9,2 → 1,6 ms/khung).
  Các khung hiện ra không đổi (16 cảnh ghi lại giống từng byte). Trên kit 16/10/2026 (bản `demo`, tự chơi, 20 mẫu
  `ui` mỗi màn hình): thời gian khung Tetris 15,4 → 3,8 ms, Pong 13,8 → 2,6, Flappy 35,0 → 16,9, khối 3D 28,6 → 8,9,
  mê cung 31,8 → 19,4 ms; bảng đủ trong `docs/huong-toi-uu-sau-v1.3.9.md`.
- **Modbus slave không chặn.** HAL RS485 thêm `hal_rs485_send()` (gửi bằng ngắt TXE, ngắt TC nhả chân hướng),
  `hal_rs485_tx_busy()`, `hal_rs485_rx_idle_ms()`; `hal_rs485_write()` giữ nguyên cách dùng. `mb_slave_poll()` chỉ gọi
  nanoMODBUS khi đường dây đã im t3.5 (5 ms ở 9600) và không đang gửi, nên không còn giữ CPU 146 – 266 ms mỗi khung.
  COMMIT của OTA: `task_fw` cài và reset sau khi câu trả lời ra hết. Master và gateway không đổi. Thử trên kit
  16/10/2026 (bản `demo`, bộ chuyển USB–RS485, 9600 baud): đọc/ghi 16..19 đúng, exception 2 khi vắt hai khối, thanh
  ghi không có, ghi vùng chỉ đọc; 841 cặp ghi + đọc liên tục trong 60 s không lỗi, không sai dữ liệu, thời gian khung
  không đổi; OTA `ak_mb.py flash` 58 KB trong 78 s, kit tự chạy lại; ngắt OTA giữa chừng thì firmware đang chạy không
  bị đụng; Pong qua RS485 (`hal_rs485_write()` nay gửi bằng ngắt) chơi được với `pong_peer.py`. Bản `app` và master
  chưa thử trên kit.
- **Kernel:** `task_post_pure_msg_try()`, `task_post_common_msg_try()` (pool đầy trả 0 thay vì FATAL);
  `AK_DYNAMIC_MSG_POOL_SIZE=0` bỏ message động cùng `malloc/free`. App của base build với 0 (`APP_KERNEL_DEFS`,
  `platformio.ini`): app −328 B flash, +148 B RAM trống. Test: `test_kernel` thêm 2 ca, `test_kernel_nodyn`.
- **Demo:** `ui_scratch()`, 320 B dùng chung cho bộ đệm lớn của màn hình đang mở (thân rắn của Snake, lưới Life và
  sao của Saver): −376 B RAM.
- **Build và CI:** CMake `-DAK_APP=demo|kit_tools|remote` (lấy cả thư mục `src/demo`, `src/remote`; remote kiểm vừa
  52K), `make stm32-mbmaster/-demo/-kit-tools/-remote/-all`. CI build mọi bản và chặn khi RAM còn cho heap + stack
  dưới 3 KB; tải lên thêm `app.img` của demo. `make test-fast`: test cắt điện lấy mẫu mỗi 7 thao tác (18 s thay cho
  76 s); `make test` và CI vẫn chạy đủ.
- **Tài liệu `docs/huong-toi-uu-sau-v1.3.9.md`**: hướng tối ưu dựa trên số đo của chính base (kích thước mọi bản build,
  thời gian bus OLED tính từ khung của `test_demo`, chỗ chặn của Modbus RTU, RAM của từng màn hình demo). Công cụ
  `tools/oled_bus_cost.py` tính thời gian bus OLED mỗi khung từ file `test_demo <thư mục> record`. Firmware không đổi.

## ak-mcu-base v1.3.9 — 13/10/2026

App **1.3.9**, bootloader **1.3.0**.

**Nâng cấp từ v1.3.8: đọc mục "Bẫy khi nâng cấp" bên dưới trước.** Bootloader 1.3.0 phải nạp bằng ST-Link, và app đầu tiên sau đó cũng phải nạp bằng ST-Link.

Bootloader **1.3.0**: xác nhận ảnh và quay lui. Log bản ghi trên flash SPI, trạm thời tiết giữ số liệu qua reset.
Rà lại toàn bộ mã để gọn và nhẹ hơn.

### Xác nhận và quay lui (confirm + rollback)

- Phân vùng mới **BACKUP** 116K trên flash SPI ở `0xA0000` (`FLASH_PART_BACKUP` trong HAL; board staging trong
  flash trong không có, mọi lệnh quay lui bị bỏ qua có thông báo). Trước khi cài, bootloader chép ảnh đang chạy sang
  BACKUP (header chép sau cùng, kiểm CRC) rồi mới chép STAGING → APP.
- Ảnh mới chạy **thử**: `boot_ctrl` có thêm `trial`, `trial_boots`, `rolled_back` (bản ghi 24 B, vẫn trong ô 32 B;
  **bootloader cũ không đọc được**, nạp bootloader 1.3.0 trước). Mỗi lần bootloader khởi động ảnh thử là một lượt, ghi
  EEPROM trước khi nhảy. App xác nhận bằng `boot_ctrl_confirm()`: `task_system` làm sau `APP_FW_CONFIRM_MS` (30 s; 0 =
  dự án tự gọi), hoặc shell `fw confirm`, hoặc lệnh giao thức `0x09`. Chưa xác nhận sau 3 lượt thì bootloader cài lại
  BACKUP và xoá header STAGING. `fw rollback` / `ak_fw.py rollback` / lệnh `0x0A` quay lui chủ động. APP hỏng mà
  STAGING không dùng được thì cứu từ BACKUP.
- Shell: `fw` (trạng thái thử, ảnh backup), `info` và `verify` in thêm phân vùng backup; app báo lúc khởi động nếu đang
  chạy ảnh do quay lui khôi phục.
- Test: `boot_decide` 14 ca mới; cài kèm sao lưu, quay lui vì không xác nhận, quay lui theo lệnh, cứu từ BACKUP, cắt điện
  ở từng thao tác flash của cài (10.081 điểm) và của quay lui; giả lập đầu-cuối thêm 12 bước (thử, xác nhận, quay lui
  theo lệnh, reset 3 lần không xác nhận → quay lui). Bootloader 8,2 → 9,3 KB (ngân sách CI 10 KB).
- **Đã chạy trên AK Base Kit 3.0 (13/10/2026):** bootloader 1.3.0 nạp bằng ST-Link; app của `main` nạp bằng ST-Link; sau đó một lần OTA qua UART đi đúng đường mới: ảnh đang chạy được sao sang BACKUP, ảnh mới chạy thử "boot 1 of 3", tự xác nhận sau 30 s, `fw` báo "image confirmed". Chương trình khối, các màn hình và nhật ký sự cố bình thường.
- **Bẫy khi nâng cấp:** bản ghi `boot_ctrl` đổi kích thước nên CRC cũ không còn khớp. Kit đang chạy app **cũ** thì sau khi nạp bootloader 1.3.0, OTA ảnh mới **không được cài** (ảnh nằm trong STAGING, lệnh cài bị bỏ qua, `installs` đứng nguyên). Lần đầu phải nạp app mới bằng ST-Link vào `0x08003000`; từ đó OTA chạy bình thường.
- **Quay lui đã thử trên kit (13/10/2026):** (1) ảnh thử không bao giờ xác nhận (build `APP_FW_CONFIRM_MS=0`): chạy thử lượt 1, 2, 3, lần reset kế tiếp bootloader tự cài lại ảnh trước từ BACKUP, xoá STAGING, `fw` báo "restored by a rollback"; (2) ảnh đã xác nhận, `fw rollback`: kit reset, bootloader cài lại BACKUP và app lên đúng ảnh cũ. Nhật ký sự cố trống cả hai lần.
- Chưa thử trên kit: cắt điện giữa lúc bootloader chép ảnh (chỉ có test mô phỏng 10.081 điểm), biến thể ký số (bootloader 16K đổi bản đồ flash), gateway với W5500, port STM32G071 (chỉ chạy trong Renode).

### Port thứ hai: STM32G071RB, chạy trong Renode

- `port/stm32g071` (NUCLEO-G071RB, Cortex-M0+ 16 MHz): viết thẳng trên thanh ghi với header CMSIS
  (`third_party/stm32g0`, Apache-2.0), không HAL/LL. Flash trang 2K ghi double word; NVM giả lập bằng hai trang
  flash cuối luân phiên có số thứ tự; BOOT 12K, APP 56K, STAGING 56K trong flash trong; console USART2 (COM ảo của
  Nucleo), LED PA5; stub HardFault thuần Thumb-1. Không sửa dòng nào phía trên HAL. `make stm32g0`
  (`-DAK_PORT=stm32g071 -DAK_MODBUS=none`): boot 8,0 KB, app 13,9 KB.
- **Renode:** `tests/renode/stm32g071.resc` và `.robot` (3 ca: boot kiểm ảnh và khởi động app, shell trả lời,
  không có app thì vào loader), `make renode`, job CI `stm32g071-renode` tải Renode 1.15.3 portable. Hai việc phải
  làm cho Renode: vùng nhớ 12K tại địa chỉ 0 chép `boot.bin` (reset lấy SP/PC ở 0), và linker script ghi `AT > RAM`
  cho `.bss`/`._heap` (sửa cả hai port, `.bin`/`.img` không đổi). Ghi flash trong Renode chưa chạy (mô hình F4).
- Chi tiết: `docs/porting.md`. **Chưa có board thật**, chưa có RS485 trên port này.

### Gateway Modbus TCP ↔ RTU qua W5500 (`AK_MODBUS=gateway`)

- HAL mới `hal/hal_tcp.h` (một socket server, đọc không chặn); driver `port/stm32l151/w5500.c` cho mô-đun W5500 trên
  J6 (SPI1 dùng chung với NOR, CS PA4); port host giả lập bằng hàng đợi RAM. Dịch vụ `services/modbus/mb_gateway.c`:
  server nanoMODBUS TCP chuyển FC 03/04/06/16 sang `mb_master` trên RS485 theo unit id của MBAP; slave im lặng →
  exception 4. Vai trò app mới `APP_MODBUS_GATEWAY` (`pio run -e app_gateway`, `make stm32-gateway`), cấu hình IP tĩnh
  `APP_NET_*` trong `app.h`, shell `mb` in trạng thái. Chi tiết: `docs/modbus.md`.
- Test: client TCP giả trong `test_modbus` (55 kiểm tra: đọc/ghi, exception, lệnh không hỗ trợ, slave im, unit khác,
  client rớt giữa khung, khung cắt, không có chip). App gateway 25,1 KB flash. **Chưa thử với W5500 thật.**

### Ký số ảnh (ECDSA P-256), biến thể build `AK_IMAGE_SIGN`

- Header ảnh có `sig_type` (offset 44) và chữ ký `r||s` (offset 48) trên SHA-256(48 byte đầu header ‖ nhị phân).
  Bootloader và app xây với `FW_IMAGE_SIGN=1` từ chối ảnh không ký / sai khoá (`bad signature`, mã lỗi 14). Xác
  minh bằng `third_party/p256-m` (Apache-2.0) và `common/sha256.c` mới. Bootloader biến thể này 16K (`PORT_IMAGE_SIGN`,
  APP từ `0x08004000`), linker lấy kích thước boot qua `--defsym __boot_size`.
- Công cụ: `tools/ak_sign.py` (keygen, pubkey-c, verify), `mkimage.py … --sign-key`, `mkimage.py info --pub`; CMake
  `-DAK_IMAGE_SIGN=ON` (`make stm32-sign`), PlatformIO `image_sign = 1`. Khoá riêng **không nằm trong repo**: `python tools/ak_sign.py devkey` tạo cặp khoá phát triển
  trong `tools/keys/` (git bỏ qua) và ghi khoá công khai vào `services/fw/fw_pubkey.c`; CI tạo một cặp dùng một lần.
  Test ký trên máy tính tự tạo khoá lúc chạy (`tests/test_pubkey.c`).
- Test: SHA-256 (ba vector chuẩn + 1 MB stream); ảnh không ký, sửa 1 byte có vá CRC, sửa trường header, sửa chữ ký,
  ký bằng khoá khác đều bị từ chối; ảnh ký đúng cài được qua bootloader; APP không ký không chạy. CI build thêm biến
  thể ký (ngân sách bootloader 14 336 B; hiện 13 224 B). **Chưa chạy trên kit; chưa đo thời gian verify trên M3.**

### Log bản ghi trên flash (`services/sys/flog.c`)

- Vòng ghi bản ghi cố định trên flash xoá theo sector: header `[magic][seq][~seq]` mỗi sector, CRC16 từng bản ghi,
  không chỉ mục trong RAM, mount lại từ flash. Bản ghi cắt điện giữa chừng bị bỏ qua, log ghi tiếp. Cách dùng:
  `docs/tien-ich.md` mục 2b.
- **Trạm thời tiết** ghi mẫu nhịp 1 phút / 15 phút vào 16 sector cuối kho media (dưới sector chương trình khối); reset
  xong đồ thị và nhịp quay lại từ log; `th log` in CSV kèm giờ RTC, `th clear` xoá. Nhịp 1 giây vẫn chỉ trong RAM.
- Test: ghi đầy vòng nhiều lần, cắt điện ở từng thao tác (296 điểm) rồi mount lại; nạp lại đồ thị sau reset trong
  `test_demo`. Demo 56,7 → 59,9 KB flash.

### Gọn và nhẹ hơn

Không đổi giao thức.

Số đo (arm-none-eabi-gcc 13.2, `-Os -flto`), trước → sau:

| Bản | Flash | RAM tĩnh (`.bss`) |
|---|---|---|
| boot | 8 700 → 8 224 B (−476) | không đổi |
| app | 21 104 → 21 064 B | 3 320 → 2 936 B (−384) |
| demo | 56 988 → 56 748 B (−240) | 8 184 → 7 800 B (−384) |

- **Kernel:** bỏ năm trường `if_*` trong `ak_msg_t` (base này không đọc chúng): header message từ 16 còn 8 byte, ba
  pool message nhỏ đi 384 B. `timer_set()` gọi lại trên timer đang chạy giờ nhận cả duty và loại mới (trước chỉ dời
  hạn: timer chu kỳ đặt lại thành một lần vẫn lặp mãi). Test `test_timer_rearm_changes_type`.
- **`xprintf`:** `%05d` của −5 in `-0005` thay vì `000-5`. Test `test_xprintf_numbers`.
- **Log:** phần đầu `[ms] I TAG: ` in bằng một hàm `ak_log_hdr()` (`common/ak_log.c`, file mới) thay vì một lời gọi
  `xprintf` bốn tham số ở từng chỗ ghi log.
- **Ảnh firmware:** `fw_image_verify()` và `fw_update_finish()` đọc thẳng vào header của người gọi, bớt hai bản 256 B
  trên stack khi COMMIT (đỉnh stack −512 B). So tên board bằng vòng lặp nhỏ thay cho `strncmp` (gcc đổi nó thành
  `strcmp` của newlib, 440 B, trong bootloader). Vì mã bootloader đổi: bootloader 1.2.2 (rồi 1.3.0 với quay lui, ở trên).
- **Nhật ký sự cố:** `crash_log_count()` tìm bản ghi mới nhất một lần rồi đếm, thay vì gọi `crash_log_read(n)` n lần
  (mỗi lần quét cả 8 ô và tính CRC16). `task_poll_modbus` chỉ cập nhật thanh ghi số sự cố mỗi giây một lần thay vì mỗi
  vòng lặp kernel.
- **Demo:** `gfx_fill()` và `gfx_invert()` làm theo byte mỗi cột và trang thay vì từng điểm (`ui_title` đảo 128 × 9 điểm
  gần như mọi khung); hộp GAME OVER của năm trò dùng chung `ui_game_over()`. Test `test_gfx_rect_ops` so với định
  nghĩa từng điểm trên 400 hình chữ nhật ngẫu nhiên; 16 cảnh ghi bằng `test_demo record` giống bản cũ từng byte.
  `test_demo` (ASan) chạy nhanh gấp đôi.
- **PlatformIO:** thêm `-Wshadow -Wundef -Wdouble-promotion -Werror` như bản build CMake; demo, kit_tools và remote
  biên dịch sạch với bộ cờ này.
- Kiểm: `make test` 7/7 trên máy tính; boot, app, demo, kit_tools, remote build sạch với arm-none-eabi-gcc 13.2; bản
  web dựng lại (`docs/play/`). **Chưa chạy trên kit thật.**

## ak-mcu-base v1.3.8 — 12/10/2026

App **1.3.8**, bootloader **1.2.1** (không đổi). Bản đầu tiên phát hành từ cấu trúc repo mới (`src/`).

- **Lập trình kéo thả** (`docs/play/blocks.html`): kéo các khối (đèn, còi, nút, màn hình, cảm biến, biến, vòng lặp),
  bấm Chạy, chương trình chạy ngay trên kit trong trang hoặc gửi sang kit thật qua USB (Web Serial). Trang dịch khối
  thành bytecode; firmware có máy ảo `demo/blk_vm.c` và màn hình mới **Blocks** (màn hình thứ 17) chạy nó, mỗi khung
  tối đa 400 lệnh nên không giữ CPU. Chương trình được lưu ở sector cuối của kho media. Lệnh shell `blk`; ba lệnh mở
  rộng `0x48`–`0x4A` của giao thức console. Bảy ví dụ. Bản demo tốn thêm 0,9 KB RAM (768 byte là bộ đệm chương trình).
- Đã kiểm lập trình kéo thả: test C 1200 phép kiểm (máy ảo, lỗi, bảy ví dụ chạy trong máy ảo thật), test bộ dịch 98
  phép kiểm dưới Node, trang chạy trên trình duyệt với kit giả lập. **Trên AK Base Kit 3.0 (12/10/2026):** gửi ba ví dụ (counter, thermo, ball) qua UART bằng đúng ba lệnh `0x48`–`0x4A`, mỗi lần dưới 0,1 s; màn hình chụp bằng `ak_screen.py` đúng như bản giả lập; nút B1 đếm đúng; sau khi reset, mở **Blocks** là chương trình đã lưu chạy lại; nhật ký sự cố trống. Nút "Gửi sang kit thật" bấm từ trình duyệt (Web Serial) cũng đã chạy. Rút USB rồi cắm lại (13/10): kit mở **Blocks** là chương trình đã lưu (85 byte) chạy lại; trước khi mở màn hình đó, lệnh `blk` còn báo "no program" vì chương trình chỉ được đọc từ flash lúc vào màn hình. Chưa thử: ví dụ nhiệt kế với cảm biến thật (kit thử không cắm SHT45, hiện 0).
- **Mô hình 3D của AK Base Kit 3.0** (`docs/kit/ak-base-kit-3i0-3d.html`): trang tự chứa, xoay được, nhấn vào linh kiện
  thì hiện thông tin; kèm hai ảnh dựng mặt trên và mặt dưới. Gắn vào hai README, tài liệu demo, trang web và trang chạy thử.
- **Kit làm dụng cụ đo trên bàn** (`src/demo`, `src/port/stm32l151`): lệnh `i2c` quét và đọc cổng I2C1, có trong mọi bản
  demo. Bản build mới `kit_tools` (demo cộng thêm) có lệnh `spi` bắt gói SPI trên các chân J6 và lệnh `rf` nghe / phát
  bằng nRF24L01+. Hai lệnh sau tách riêng vì bộ đệm bắt SPI chiếm 5,6 KB RAM. Cách dùng: `docs/demo-kit.md`.
- Ba công cụ này đã được dùng trên kit thật để dò một giao thức radio; trong repo chúng mới qua biên dịch (cả bản web)
  và bộ test hiện có, chưa có test riêng. `rf` viết cho kit có CSN của mô-đun ở PB9; trên kit 3.0 (CSN ở PA4) chưa thử.
- **Bản build `remote`** (`src/remote`): AK Base Kit 2.1 (chip 64K, nRF24L01+ và OLED SSD1306 trên bo) làm tay điều
  khiển thử nghiệm cho thiết bị đã dò bằng `rf`: menu trên OLED và ba nút chọn nội dung gói, nRF24 phát liên tục. Lệnh
  shell `rc`. Đã chạy trên kit 2.1 thật (màn hình lên, thiết bị ghép cặp và đọc đúng gói); ba nút mới bấm qua `rc`.
- Bản `remote` nhận lệnh từ máy tính: `rc on` / `rc off`, `rc p <13 byte>` đặt cả gói, `rc wd <ms>` là bộ canh kéo ga
  về `00` khi máy tính ngừng gửi, `rc t <ms>` đổi nhịp phát lúc đang chạy, `rc a 0|1` cho byte 0 xen kẽ `DD` / `D5` theo PID. Đã thử trên kit 2.1 thật (đặt gói, bộ canh
  nhả và nối lại, thiết bị quay motor theo gói đặt từ máy tính); chưa có test trên host.
- `kit.c`: thêm tuỳ chọn build `KIT_LCD_SSD1306` (bật bơm điện áp cho OLED 0,96") và `KIT_LCD_COL_OFFSET` (SH1106);
  mặc định không đổi gì với kit 3.0. Thêm `kit_bus_probe()` để dò bus màn hình và bus I2C1.

## Cấu trúc repo đổi — 07/10/2026

`ak-mcu-base` thành base chính của repo, và **repo đổi tên từ `ak-base-kit-pio` thành `ak-mcu-base`**. Không đổi mã
firmware, không tăng số phiên bản.

- Địa chỉ mới: <https://github.com/hohoanganh/ak-mcu-base>, trang web <https://hohoanganh.github.io/ak-mcu-base/>.
  GitHub tự chuyển hướng địa chỉ repo cũ (kể cả `git clone`); địa chỉ trang web cũ thì không.

- Nội dung `ak-mcu-base/` chuyển ra **gốc repo**: mã nguồn firmware gom vào `src/` (`kernel/`, `hal/`, `common/`,
  `services/`, `boot/`, `app/`, `demo/`, `port/`), còn `tests/`, `tools/`, `third_party/` ở gốc. Tài liệu của nó từ
  `ak-mcu-base/docs/` vào `docs/`, README của nó thành `docs/ak-mcu-base.md`.
- Base cũ (`sources/`, `platformio.ini` và các script `pio_*.py`, `release/`, `tests_host/`, `tools/`, `boards/`, tài liệu
  của nó) chuyển nguyên vẹn vào **`legacy/`**. Build base cũ: `cd legacy` rồi `pio run` như trước.
- SPL và CMSIS mà base chính dùng nay nằm trong `third_party/stm32l1/` (trước mượn từ `sources/`), nên base chính không còn
  phụ thuộc thư mục nào của base cũ. `tools/new_project.py` chép thẳng từ gốc repo.
- CI chạy cho mọi thay đổi ngoài `legacy/` và tài liệu.
- Bản build từ cấu trúc mới (bootloader 1.2.1, demo 1.3.7) đã nạp và chạy trên AK Base Kit 3.0: OTA qua UART và qua
  RS485, Modbus, đủ 16 màn hình. Bootloader giống từng byte với file của bản v1.3.7; ba ảnh app cùng kích thước nhưng
  khác thứ tự liên kết.
- Đường dẫn trong các mục bên dưới là đường dẫn **của thời điểm đó**. Các tag cũ giữ nguyên cấu trúc cũ; link tới
  `…/blob/main/ak-mcu-base/…` hoặc `…/blob/main/sources/…` từ bên ngoài sẽ không còn mở được, dùng link theo tag.

## ak-mcu-base v1.3.7 — 07/10/2026

App **1.3.7**, bootloader **1.2.1**. Bản đầu tiên có firmware sửa theo schematic của kit 3.0, và sửa hai lỗi lộ ra
khi chạy trên kit thật.

- **LED nhịp tim đúng pha với bo** (`port/stm32l151`): LED đỏ ở PB8 nối giữa 3V3 và chân, sáng khi chân ở mức thấp.
  `hal_led_set(1)` giờ kéo chân xuống thấp; thêm `PORT_LED_ACTIVE_LOW` (mặc định 1, đặt 0 cho bo có LED sáng mức cao).
  Chân được đặt về mức "tắt" trước khi thành ngõ ra.
- **Bootloader 1.2.1**: cùng bản sửa LED. Bootloader 1.2.0 bật LED suốt khoảng 73 ms nó kiểm ảnh, nhìn thấy như một
  cái chớp lúc khởi động; muốn hết chớp phải nạp cả bootloader mới (ST-Link, `0x08000000`). Ngoài LED, 1.2.1 không
  khác 1.2.0: app 1.3.7 vẫn chạy với bootloader 1.2.0.
- **Chân CSN của mô-đun SPI ở J6 là PA4**: `PORT_KIT_NRF24_CSN` (mặc định tắt) giờ giữ PA4 ở mức cao thay cho PB9;
  kit đời trước ghi đè ba macro `PORT_NRF_CSN_*`.
- **Sửa `ui dump` trên màn hình đổi liên tục** (`demo/`): ở màn hình System, trang cuối không bao giờ được gửi và bản
  chụp một lần không tự tắt, nên `tools/ak_screen.py shot` báo "no screen received". Các trang giờ chia lượt xoay vòng
  (`ui_stream_batch()`), một lần dump gửi mỗi trang đúng một lần rồi dừng.
- **Sửa bộ đọc khung của `tools/ak_fw.py`**: một byte `0xA5` lạc trong log của board đứng ngay trước khung trả lời làm
  khung đó không được đọc và lệnh hết giờ. Test mới `tests/test_ak_fw.py` (12 phép kiểm), chạy trong `ctest`.
- Sơ đồ cổng nối: J5, J8, J11 (Grove 2,0 mm) là jack tuỳ chọn, bo thương mại không hàn; J9 chân 4, 6, 9 là GND
  (đã xác nhận trên kit).
- **Đã chạy trên AK Base Kit 3.0 (07/10/2026)**, với mã của bản này build trước khi tăng số phiên bản: LED đúng pha và
  không chớp lúc khởi động, `ui open` mở đủ 16 màn hình, `ak_screen.py shot` chụp đủ 16 màn hình. Các file đính kèm
  của bản phát hành (mang số 1.3.7 / 1.2.1) chưa được nạp lại lên kit.
- Chưa thử: tuỳ chọn giữ CSN ở PA4 với mô-đun nRF24 cắm thật.

## ak-mcu-base v1.3.6 — 06/10/2026

App **1.3.6**, bootloader không đổi (1.2.0). Mã firmware không đổi so với v1.3.4 ngoài số phiên bản; bản này là sơ đồ và sửa tài liệu theo schematic.

- **Sơ đồ chân đã đối chiếu với schematic AK MCU KIT 3.0.** Mọi chân firmware dùng đều khớp. Sửa theo schematic: PA4,
  PA8, PB1 không phải chân trống mà ra cổng SPI mở rộng J6 (CSN, CE, IRQ); PA15 vừa là reset màn hình vừa có trên J13;
  BOOT0 có nút bấm; LED đỏ sáng ở mức thấp.
- **Sơ đồ các cổng nối** (`docs/kit/connectors.svg`, `tools/connectors_svg.py`): 10 cổng, từng chân một.
- **SHT45 không nằm trên bo**: không có trong schematic và BOM bản 3.0. Tài liệu trước đây gọi nó là cảm biến của kit;
  đã sửa thành mô-đun cắm ngoài qua cổng I2C.
- **Bảy sơ đồ khối mới** (`tools/diagrams_svg.py`): sơ đồ khối của bo, cây nguồn, các lớp phần mềm và ba nơi cùng mã
  nguồn chạy, bản đồ bộ nhớ, năm bước cập nhật firmware, cách demo chạy trên kernel, đường dây và nhịp truyền của Pong.
  Đặt vào README (hai ngôn ngữ), README của `ak-mcu-base`, tài liệu demo và tài liệu Modbus.
- Ghi lại hai chỗ firmware chưa theo schematic (LED ngược pha, chân CSN của J6) trong tài liệu demo.

## ak-mcu-base v1.3.5 — 06/10/2026

App **1.3.5**, bootloader không đổi (1.2.0). Mã firmware không đổi so với v1.3.4 ngoài số phiên bản; bản này thêm ảnh và sơ đồ chân vào tài liệu.

- **Ảnh kit và sơ đồ mặt trên của bo** trong `docs/kit/`, lấy từ repo của AK Foundation (giấy phép MIT), cùng bản dựng
  3D của bo mạch; đưa vào README, README tiếng Anh, tài liệu demo và trang chạy thử.
- **Sơ đồ chân STM32L151CBT6 trên kit** (`docs/kit/stm32l151-pinout.svg`): 48 chân, mỗi chân ghi tên và việc nó làm
  trong firmware, tô màu theo nhóm. Sinh bởi `tools/pinout_svg.py`.

## ak-mcu-base v1.3.4 — 06/10/2026

App **1.3.4**, bootloader không đổi (1.2.0).

- **Lệnh `ui open <tên>`**: mở thẳng một màn hình theo một phần tên trong menu (`ui open tetris`, `ui open 3d`,
  `ui open menu`), không phải bấm qua menu. Dùng được trên kit qua shell.
- **Trang chạy thử sống ngay khi mở:** kit tự đi một vòng qua các màn hình đẹp nhất cho tới khi có người bấm nút;
  hàng nút tắt tới từng màn hình; link chia sẻ dạng `play/?screen=tetris`.
- **Trang [hai kit chơi Pong](https://hohoanganh.github.io/ak-mcu-base/play/pong.html):** hai bản firmware cạnh nhau trên một trang, nối bằng đường
  RS485 giả lập; xem được ai làm chủ, số byte qua lại, và rút dây để thấy chúng tự quay về chơi một mình.
- **`port/web/kit-ui.js`**: một kit thành một thành phần nhúng được vào trang bất kỳ; trang giới thiệu của dự án
  giờ có kit chạy thật ngay đầu trang.
- **README**: GIF 19 giây các cảnh đẹp nhất (`tools/highlights_gif.py`), nút chạy thử, bảng lối vào;
  thêm [README.en.md](README.en.md) tiếng Anh.

## ak-mcu-base v1.3.3 — 06/10/2026

App **1.3.3**, bootloader không đổi (1.2.0). Mã firmware không đổi so với v1.3.2 ngoài số phiên bản; bản này thêm clip mẫu và tool.

- **Clip mẫu của màn hình Video là logo AK Foundation xoay 3D** (6 giây, 19 KB), thay cho clip đốm trắng; bản chạy trên
  trình duyệt cũng dùng clip này. Logo thuộc về AK Foundation.
- **`tools/spin_clip.py`**: biến một logo bất kỳ (ảnh trên nền trơn) thành clip `.akv` xoay 3D: tấm có bề dày, mặt trước
  trắng, cạnh bên tô dither, mặt sau cũng đọc được.

## ak-mcu-base v1.3.2 — 06/10/2026

App **1.3.2**, bootloader không đổi (1.2.0).

- **Pong qua RS485** (màn hình mới của demo): hai kit nối chung dây RS485 chơi với nhau; kit có số ngẫu nhiên lớn hơn
  làm chủ, tính đường bóng và gửi trạng thái 20 lần/giây, kit kia trả lời vị trí thanh đỡ. Một mình thì kit tự chơi
  với máy và phát "hello" chờ kit thứ hai. Khi màn hình này mở, cổng RS485 thuộc về trò chơi, Modbus tạm nghỉ.
  `tools/pong_peer.py` cho máy tính đóng vai kit thứ hai qua USB-RS485. Đã thử trên kit với máy tính ở cả hai vai:
  20 khung/giây, 300/300 khung được trả lời, không khung hỏng; rời màn hình thì Modbus trả lời lại bình thường.
- **Xem màn hình kit trên máy tính:** lệnh shell `ui dump` (một lần) và `ui stream` (liên tục) gửi các trang màn hình
  đã đổi dưới dạng dòng chữ nén PackBits, có CRC. `tools/ak_screen.py` chụp ảnh (`shot`), quay GIF (`record`) hoặc mở
  cửa sổ xem trực tiếp (`live`), bấm phím 1 2 3 thay cho ba nút. Đo trên kit: 19 hình/giây với màn hình đồng hồ,
  9,5 hình/giây khi cả màn hình đổi liên tục.
- **Bản chạy trên trình duyệt** (<https://hohoanganh.github.io/ak-mcu-base/play/>): firmware (kernel, shell, Modbus slave, bộ demo) biên dịch sang
  WebAssembly bằng Emscripten. `port/web/web_main.c` đóng vai bo mạch, `port/host` đóng vai chip; trang web có màn hình,
  ba nút (chuột, chạm, phím 1 2 3), còi, console gõ lệnh shell, nút gây FATAL để xem nhật ký sự cố, nạp clip `.akv`.
  Mở trang ở hai tab là hai bản firmware chơi Pong với nhau: byte RS485 đi giữa hai tab. Dựng lại bằng
  `python port/web/build.py`; CI có thêm bước build này. `app_main()` tách ra `app_init()` để port không có vòng lặp
  vô hạn gọi được `task_run_once()`.
- **Máy hiện sóng mini** (màn hình Scope): đồ thị cuộn 120 điểm, tự co giãn. Số liệu vào bằng lệnh shell
  `plot <số>` hoặc ghi thanh ghi Modbus 16. `tools/ak_plot.py` gửi sóng sin, nhiễu, hoặc số đọc từ stdin, qua UART
  hay RS485. Đã thử cả hai đường trên kit.
- Tổng kết phiên làm việc 06/10/2026: [ak-mcu-base/docs/tong-ket-2026-10-06.md](docs/tong-ket-2026-10-06.md).
- Unit test demo: 456 kiểm tra.

## ak-mcu-base v1.3.1 — 06/10/2026

App **1.3.1**, bootloader không đổi (1.2.0). Bản này chủ yếu thêm bộ demo cho AK Base Kit.

- **Ba demo mới trên kit:** Dino runner (B1 nhảy, B2 cúi), khối 3D quay (lập phương, bát diện, kim tự tháp; khung dây
  hoặc tô bóng bằng dither, chỉ dùng số nguyên), máy hát RTTTL trên còi với 6 bài, các nốt chạy trên màn hình.
  Menu cuộn được, 7 mục. `gfx` thêm `gfx_line`, `gfx_tri` (tô theo mức xám), `gfx_bitmap`. Unit test demo: 90 kiểm tra.
  Đo trên kit: Dino tối đa 24 ms mỗi khung, 3D 34–37 ms, máy hát 4 ms.
- **Thêm ba demo nữa:**
  - *Mê cung 3D* (ray casting kiểu Wolfenstein, chỉ dùng số nguyên): B1/B2 quay, B3 đi/dừng, tìm cửa ra có sọc.
    Đo trên kit: tối đa 37 ms mỗi khung khi đang đi.
  - *Video từ flash SPI*: clip 1 bit 128×64 nằm ở vùng 0–512 KB của flash SPI (vùng OTA không dùng), chỉ lưu các
    trang thay đổi, nén PackBits, có thể lưu dạng hiệu với khung trước. Tool `tools/ak_video.py` chuyển video, GIF,
    thư mục ảnh thành `.akv` và nạp vào kit qua UART (57 KB trong 8 s). Đo trên kit: 41–47 ms mỗi khung khi đổi cả màn hình.
  - *Trạm thời tiết*: nhiệt độ, độ ẩm từ SHT45, đồ thị 96 điểm (mỗi 1 s, 1 phút hoặc 15 phút = 24 giờ); cảm biến được đọc
    mỗi giây dù đang mở màn hình nào. Lệnh shell `th`, `th csv`.
  Menu 10 mục. Unit test demo: 124 kiểm tra.
- **Thêm ba game và bộ màn hình chờ:** Tetris (giếng 10×20, B1/B2 sang ngang, B3 xoay, giữ B1+B2 để thả), Breakout,
  Invaders, và màn hình chờ gồm Game of Life (lưới 64×32, đếm hàng xóm bằng phép bit trên từ 64 bit), trường sao, plasma.
  Cả ba game đều tự chơi được với `ui auto`. Menu 14 mục. Unit test demo: 137 kiểm tra.
  Đo trên kit: Tetris 20 ms mỗi khung, Breakout 15 ms, Invaders 31 ms, màn hình chờ 45–50 ms.
- **Timer khung của demo đặt lại sau mỗi khung** thay cho timer chu kỳ: hiệu ứng đổi cả màn hình cần hơn 50 ms thì chỉ
  chạy chậm đi, không dồn message vào pool.
- **Giao thức nạp (`fw_proto`) có chỗ mở rộng:** lệnh từ `0x40` trở lên được chuyển cho hàm `ext` của ứng dụng
  (demo dùng để ghi file vào flash SPI). Bootloader không đổi hành vi.
- **`demo/kit.h`** thêm SHT45 (`kit_sht_start/read`) và kho file trên flash SPI (`kit_store_*`).
- **Demo tự chơi:** lệnh shell `ui auto` cho các game và mê cung tự chạy, máy hát tự phát lần lượt; bấm nút là
  giành lại quyền điều khiển.
- **Ảnh động cho tài liệu:** `tests/test_demo <thư mục> record` ghi từng khung từ mã vẽ thật, `tools/demo_gif.py`
  dựng thành GIF (README, [ak-mcu-base/docs/demo-kit.md](docs/demo-kit.md)).
- **Huy hiệu Repo Traffic** ở đầu README.
- **Ý tưởng demo tiếp theo**, kèm nguồn tham khảo: [ak-mcu-base/docs/y-tuong-demo.md](docs/y-tuong-demo.md).

## ak-mcu-base v1.3.0 — 06/10/2026

App **1.3.0**, bootloader không đổi (1.2.0).

- **Modbus RTU trên RS485** (nanoMODBUS): slave với bảng thanh ghi khai theo khối, master, OTA qua khối thanh ghi
  `0xF000` mang ảnh `.img` của base mới; tool `tools/ak_mb.py`. Vai trò chọn lúc build (`env:app` là slave,
  `env:app_mbmaster`). Unit test 174 kiểm tra. Đã kiểm trên kit: đọc/ghi thanh ghi, OTA qua RS485 (20K trong 26 s ở
  9600 baud), master.
  Tài liệu: [ak-mcu-base/docs/modbus.md](docs/modbus.md).
- **Demo trên AK Base Kit** (`env:demo`): menu, đồng hồ số (RTC hoặc tự đếm), Snake, Flappy, màn hình hệ thống trên
  OLED 128×64 + 3 nút + còi; điều khiển được qua shell (`ui`). Đã chạy trên kit; ảnh màn hình dựng từ mã vẽ thật.
  Tài liệu: [ak-mcu-base/docs/demo-kit.md](docs/demo-kit.md).

## ak-mcu-base v1.2.0 — 06/10/2026

Bootloader **1.2.0**. Từ bản này `ak-mcu-base` là base cho các dự án mới.

- **Tạo dự án mới:** `tools/new_project.py` xuất một dự án độc lập (base + SPL/CMSIS + file board), ghi `BASE_VERSION`,
  đặt tên board riêng cho sản phẩm.
- **Build PlatformIO chạy được** (trước đó hỏng ngay bước đầu: script build chạy sai thời điểm). Ảnh build bằng
  PlatformIO đã nạp và OTA trên board.
- **Nhật ký sự cố** trong EEPROM (8 bản ghi): HardFault, FATAL, watchdog, task bị bỏ đói; kèm task và signal đang
  chạy. Lệnh shell `crash`, `crash clear`, `crash test …`. **Cần bootloader từ 1.2.0.**
- **Giám sát task:** `task_system` ping mọi task mỗi giây; task không được chạy 3 nhịp thì ghi nhật ký và reset.
  Kernel thêm `task_alive_take()`, `AK_SIG_PING`, và báo port handler sắp chạy (`ak_port_note_dispatch`).
- **Đo mức dùng stack:** lệnh `stat` in số byte RAM chưa từng dùng.
- **CI:** GitHub Actions chạy test host và build STM32, giới hạn bootloader 10.240 B.
- **NVM:** `HAL_NVM_SIZE` tăng lên 256; `boot_ctrl` giữ nguyên chỗ (hai bản ghi 32 B ở đầu).
- Tài liệu: [ak-mcu-base/docs/tien-ich.md](docs/tien-ich.md) ·
  [ak-mcu-base/docs/huong-toi-uu-tiep.md](docs/huong-toi-uu-tiep.md) (các hướng tra cứu được, kèm nguồn).

## ak-mcu-base v1.1.0 — 06/10/2026

Thư mục [`ak-mcu-base/`](docs/ak-mcu-base.md) đánh số riêng (tag `ak-mcu-base-v<x.y.z>`), không ảnh hưởng
base trong `sources/`. Bản đầu tiên chạy trên board thật; bootloader **1.1.0**.

- **Kernel:** timer mềm dùng mốc hết hạn tuyệt đối (không trôi, không nổ sớm, tick chỉ post khi đến hạn);
  ngoài task thì id hiện tại là `AK_TASK_IDLE_ID`; vòng chính kiểm hàng đợi và `WFI` trong một critical section.
- **Build:** LTO bật mặc định, boot và app nhỏ hơn khoảng 14%.
- **App:** console TX qua ring + ngắt TXE; shell có `info` (kiểm header) và `verify` (CRC toàn ảnh); dồn lệnh
  không còn gây FATAL.
- **Bootloader 1.1.0:** `boot_ctrl` hai bản ghi luân phiên, watchdog 10 s, vòng chờ flash SPI có giới hạn.
  **App từ 1.1.0 phải đi với bootloader từ 1.1.0.**
- **Tool:** `ak_fw.py` OTA ảnh 10,5K từ 5,4 s xuống 1,6 s.
- **So với base cũ:** [ak-mcu-base/docs/so-voi-base-cu.md](docs/so-voi-base-cu.md).
- README gốc viết lại gọn; các mục dài chuyển sang [docs/bat-dau-du-an-moi.md](legacy/docs/bat-dau-du-an-moi.md) và
  [docs/ota-rs485.md](legacy/docs/ota-rs485.md).

## v1.3.0 — 25/09/2026

Bootloader **không đổi mã** (vẫn 0.0.3), chỉ nâng `-DAPP_VERSION` cho khớp base.

- **Nối OTA vào firmware slave:** `app_modbus.cpp` (nhánh `TASK_MBSLAVE_EN`) gọi
  `mb_ota_init(&ota_ops)` trước `nmbs_server_create` — `ota_ops` trỏ thẳng `fw_ext_erase`,
  `fw_ext_write`, `fw_ext_checksum` (external flash, `task_fw.cpp`) và `ota_commit` (dựng
  `firmware_header_t` rồi gọi `fw_commit_app_later`, hẹn `FW_MB_OTA_COMMIT` sau 200 ms để phản
  hồi Modbus kịp ra đường truyền trước khi ghi BSF + reset). `static_assert(MB_OTA_PSK ==
  FIRMWARE_PSK)` chặn lệch magic number ngay lúc biên dịch.
- **OTA qua Modbus RS485** dùng được thật trên env `app_mbslave`: state machine bảng thanh ghi
  holding `0xF000` (BEGIN/COMMIT/ABORT, ghi khối, đọc STATUS) — chi tiết bảng thanh ghi + luật
  (bin_len bội 4, khoá state sau COMMIT, chặn broadcast unit_id 0, ops NULL thì BEGIN/COMMIT/ghi
  khối lỗi an toàn) ở [docs/superpowers/specs/2026-09-25-nanomodbus-va-ota-modbus-design.md](docs/superpowers/specs/2026-09-25-nanomodbus-va-ota-modbus-design.md).
  README có mục "Cập nhật firmware qua RS485" hướng dẫn dùng từ PC.
- **Review nhỏ:** thêm chú thích "chi dung cho RTU: unit_id 0 la broadcast" cạnh hai chỗ chặn
  `unit_id == 0` trong `mb_slave_regs.c` (không đổi hành vi).
- **Số flash:**

  | Env | v1.2.0 | v1.3.0 |
  |---|---|---|
  | `app` | 53668 B | 53772 B |
  | `app_mbslave` | 56960 B | 58120 B |
  | `boot` | 6820 B | 6820 B (không đổi) |

- **Đã kiểm trên board thật (25/09/2026):** OTA 1.3.0 → 1.3.1 → 1.3.0 qua USB-RS485 @9600
  ≈ 78 s/lượt, bootloader xóa 0,8 s + chép 2,7 s; cắt ngang giữa chừng board vẫn chạy app cũ và
  chạy lại được; 200/200 lần đọc FC03 đúng. Chi tiết + các ca thử âm:
  [docs/tinh-nang-moi-v1.2-v1.3.md](legacy/docs/tinh-nang-moi-v1.2-v1.3.md) mục 5.
- **Công cụ PC:** `python -m epcb_applib.ota` (epcb-applib ≥ 1.4.0) — kiểm bảng vector ảnh, tự
  ABORT phiên dở, xác nhận phiên bản sau khi board khởi động lại.
- **Nạp bằng ST-Link:** nếu `pio run -t upload` báo lỗi `hla_swd` (OpenOCD mới + ST-Link V2), dùng
  `ST-LINK_CLI.exe` — lệnh ở tài liệu trên, mục 4.

## v1.2.0 — 25/09/2026

Bootloader **không đổi mã** (vẫn 0.0.3), chỉ nâng `-DAPP_VERSION` cho khớp base (base đánh số chung
app + boot).

- **Modbus master:** bỏ hẳn thư viện thương mại `mbmaster-v2.9.6`, chuyển sang
  [**nanoMODBUS v1.23.0**](https://github.com/debevv/nanoMODBUS) (MIT) — port RTU trên USART2 tự viết
  (`rs485_port.c`, ring buffer + DIR RS485), **không còn dùng TIM4** (trước phải chiếm timer cho
  timeout; xem commit `feat(modbus): master chuyen sang nanoMODBUS`).
- **Thêm chế độ Modbus SLAVE:** cờ `-DTASK_MBSLAVE_EN` (loại trừ lẫn nhau với `TASK_MBMASTER_EN`,
  `#error` lúc biên dịch nếu bật cả hai) — nanoMODBUS chạy vai server trên USART2, dùng cho OTA firmware
  qua RS485. Thanh ghi demo trong `mb_slave_regs.c`: reg 0 = `(major<<8)|minor`, reg 1 = patch, reg 2 =
  uptime (giây). Vòng lặp `app_modbus_poll()` gọi từ `task_polling_mbslave`
  (`AC_TASK_POLLING_MBSLAVE_ID`).
- **Env mới `[env:app_mbslave]`** trong `platformio.ini` — kế thừa `env:app`, chỉ đảo cờ
  master → slave (`build_unflags`/`build_flags`), build ra file riêng
  `release/app_mbslave/ak_base_kit_app_mbslave_v1.2.0.bin` (không đè lên bản `env:app`).
- **Test host:** `tests_host/modbus/run_tests.sh` biên dịch `mb_slave_regs.c` + `nanomodbus.c` bằng
  gcc thường (không cần board), kiểm giá trị 3 thanh ghi demo — chạy được trên máy dev/CI.
- **Timeout master đổi:** nanoMODBUS master chờ phản hồi tối đa `APP_MB_READ_TIMEOUT_MS` = 500 ms
  (`app_modbus.h`), thay vì 100 ms của `mbmaster` cũ. Hệ quả: lệnh `modbus r` khi rút mất một thiết
  bị giờ đợi ~2 s cho thiết bị đó (thay vì ~0,4 s trước đây) trước khi báo lỗi và sang thiết bị kế.
- **Repo:** thôi đưa file `.elf` trong `release/` vào git (~1,1 MB mỗi bản, repo phình theo từng
  bản). Git chỉ giữ `.bin`; `.bin` + `.elf` của các bản đính kèm ở
  [GitHub Releases](https://github.com/hohoanganh/ak-mcu-base/releases).
- **Tài liệu:** hai guide EPCB trong repo MCP (`epcb-platformio-build`, `epcb-start-project`) cập
  nhật theo v1.1.2.
- **Số flash (env:app, không tính env:app_mbslave):**

  | Env | Trước (v1.1.2, còn mbmaster-v2.9.6) | Sau (v1.2.0, nanoMODBUS master) |
  |---|---|---|
  | `app` | 58220 B | 53668 B |
  | `app_mbslave` (mới) | — | 56960 B |
  | `boot` | 6820 B | 6820 B (không đổi) |

## v1.1.2 — 24/09/2026

Bootloader **không đổi** (vẫn 0.0.3).

- **Sửa lỗi #12:** cờ biên dịch trong `pio_build_flags.py` (`-std=gnu99`, `-std=gnu++11`,
  `-fno-use-cxa-atexit`) chưa từng tới file nguồn nào — script chỉ sửa `env`, không sửa `projenv`.
  Nay áp cho cả hai; build đúng chuẩn gnu99/gnu++11 vẫn 0 cảnh báo.
- **Sửa lỗi #13:** version app gõ cứng `0.0.0.3`. Nay `-DAPP_VERSION` là nguồn duy nhất — board in
  `App version: 1.1.2.0`, khớp tên file release.
- **Kernel:** `task_create()` kiểm thứ tự `app_task_table` khớp enum task ID, lệch thì
  `FATAL("TK", 0x08)` ngay lúc khởi động. Kernel tra task bằng chỉ số `task_table[id]`, nên thêm task
  sai chỗ trước đây làm message đi nhầm task mà không báo gì.
- **Build:** giới hạn flash riêng từng env — `board_upload.maximum_size` app 116K, boot 8K. Trước đây
  cả hai tính trên 128K của chip: phần trăm báo sai, và bootloader lớn quá 8K không bị PlatformIO
  chặn.
- **Tài liệu:**
  - Vẽ lại sơ đồ bootloader theo code thật: sau cập nhật là `UPDATE_RES` (không phải `NONE`), có nhánh
    tự vá BSF và nhánh chờ nạp UART khi flash app trống.
  - Hướng dẫn thêm task: nói rõ ràng buộc thứ tự bảng task; sửa đoạn `platformio.ini` mẫu; sửa chỗ
    ghi flash trắng đọc ra `0xFF` (trên STM32L1 là `0x00`).
  - README: bước 1 đổi sang clone theo tag (khớp hướng dẫn), bổ sung cây thư mục, thêm mục phiên bản.
  - Thêm file này. Bỏ bản HTML trùng lặp (lỗi thời, cùng nội dung với bản Markdown).
- **Chuyển sang repo cá nhân:** bỏ thông tin liên hệ công ty và link website trong tài liệu, chú thích
  code; đường dẫn `_reference/...` (chỉ có trên máy cũ) đổi thành link repo gốc AK Foundation.

## v1.1.1 — 23/09/2026 · bootloader 0.0.3

Họ lỗi "struct cấu hình SPL không khởi tạo" — rà toàn bộ 62 biến `*_InitTypeDef`, 4 chỗ là lỗi thật:
ADC đọc kênh ngoài ra 0 (#8), rác vào điện trở kéo GPIOA, đo được chân RX RS485 ở trạng thái cấm
dùng (#9), SPI của bootloader (#10), NVIC buzzer (#11). Chi tiết: [docs/known-bugs.md](legacy/docs/known-bugs.md).

## v1.1.0 — 23/09/2026 · bootloader 0.0.2

Sửa 7 lỗi nền đầu tiên (#1–#7): `HardwareSerial` `flush()` rỗng và `write()` mất tín hiệu đánh thức
TXE; bootloader kẹt "uart boot" khi BSF hỏng — nay tự kiểm bảng vector app và tự vá BSF; USART2 một chủ
với cờ mới `SERIAL2_EN` / `SERIAL2_BAUDRATE`; tắt được `TASK_MBMASTER_EN`; macro chân USART2 đúng
silicon.

## v1.0.0 — 16/08/2026 · bootloader 0.0.1

Bản đầu tiên đánh tag: port AK Embedded Base Kit sang PlatformIO, target `-t bsf`, cờ linker
`max-page-size=4` chống ghi đè bootloader khi nạp bằng `-t upload`, tích hợp MCP docs server.
