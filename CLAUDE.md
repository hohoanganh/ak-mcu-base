# CLAUDE.md

Hướng dẫn cho AI assistant làm việc trong repo này. Đọc trước khi sửa code.

Tài liệu cho người: [README.md](README.md) · [docs/ak-mcu-base.md](docs/ak-mcu-base.md) (kiến trúc, bản đồ flash,
OTA) · [docs/demo-kit.md](docs/demo-kit.md) · [docs/tien-ich.md](docs/tien-ich.md) · [docs/modbus.md](docs/modbus.md) ·
[docs/porting.md](docs/porting.md) · [CHANGELOG.md](CHANGELOG.md).

## 1. Repo này là gì

**ak-mcu-base**: nền firmware bare-metal cho STM32L151CBT6 (128K flash, 16K RAM, 32 MHz), gồm kernel AK (Active
Object, không RTOS, không preemptive), bootloader, cập nhật firmware an toàn khi mất điện, Modbus RTU và bộ demo cho
AK Base Kit. Mã phía trên HAL không include header của chip; cùng mã đó chạy trên chip, trên máy tính (unit test,
bộ giả lập `ak_sim`) và trong trình duyệt (WebAssembly).

Đây là **source base**: dự án sản phẩm được xuất ra từ nó bằng `tools/new_project.py`, không phát triển ngay trong repo.

`legacy/` là base cũ (v1.3.0, tên `ak-base-kit-pio`), chỉ còn bảo trì cho dự án đang chạy trên nó. Không sửa `legacy/`
khi việc được giao là về base chính, và không lấy mã từ `legacy/` sang mà không được yêu cầu. Quy ước của nó ở
[legacy/README.md](legacy/README.md).

## 2. Cấu trúc

```
src/            mã nguồn firmware
  kernel/       task, message, timer, fsm, tsm. Sửa ở đây phải có test trước
  hal/          giao diện phần cứng mà mọi port phải có (hal.h, hal_flash.h, hal_rs485.h, hal_tcp.h)
  common/       xprintf, CRC, log
  services/     fw (ảnh, OTA, boot_ctrl) · sys (nhật ký sự cố, flog) · modbus (nanoMODBUS: slave, master, gateway TCP)
  boot/         bootloader, không phụ thuộc chip
  app/          app mẫu: task_system, task_console, task_fw, task_modbus  ← dự án viết task ở đây
  demo/         demo trên AK Base Kit: task_ui, scr_*.c, gfx, music, video
  port/         stm32l151 (chip) · stm32g071 (chip thứ hai, chạy trong Renode) · host (máy tính, ak_sim) · web (WebAssembly)
tests/          test_kernel, test_fw, test_modbus, test_demo (C) · test_ak_fw.py, test_sim_ota.py · renode/ (STM32G071)
tools/          ak_fw.py, ak_mb.py, mkimage.py, new_project.py, ak_screen.py, tool sinh sơ đồ
third_party/    nanomodbus, p256-m (ECDSA), stm32l1 (SPL + CMSIS), stm32g0 (CMSIS). Mã của bên khác: không sửa
docs/           tài liệu, và trang web GitHub Pages (index.html, play/, kit/)
legacy/         base cũ
```

Trong tài liệu và chú thích mã, đường dẫn kiểu `port/stm32l151`, `demo/scr_pong.c` tính từ `src/`.

## 3. Lệnh

```bash
# test trên máy tính (Linux / WSL), có ASan + UBSan
make test                      # hoặc: cmake -S . -B build/host && cmake --build build/host -j && ctest --test-dir build/host -LE fast_only
make test-fast                 # vài giây: test cắt điện chỉ lấy mẫu (mỗi 7 thao tác); trước commit vẫn chạy make test
make sim                       # bộ giả lập: bootloader + app, gõ help

# firmware
pio run -e boot -e app -e app_mbmaster -e app_gateway -e demo   # .pio/build/<env>/app.img, boot: firmware.bin
pio run -e kit_tools                               # demo + lệnh spi (bắt SPI trên J6) và rf (nRF24)
pio run -e remote                                  # kit 2.1 (64K): tay điều khiển thử nghiệm, OLED + 3 nút + nRF24 (src/remote)
make stm32-all                                     # mọi bản STM32L151 bằng CMake như CI: stm32, -internal, -sign,
                                                   # -gateway, -mbmaster, -demo, -kit-tools, -remote, -rc-tx, -rc-rx, -fc
make stm32g0 renode                                # port STM32G071, rồi chạy trong Renode (tests/renode)

# bản chạy trên trình duyệt, ghi vào docs/play/ (cần Emscripten)
python src/port/web/build.py
```

- **Thay đổi chưa qua `make test` thì chưa xong.** Sửa `src/demo` phải chạy `test_demo` và dựng lại bản web rồi commit
  `docs/play/`. Sửa `tools/ak_fw.py` có `tests/test_ak_fw.py`.
- Môi trường không có toolchain thì **nói rõ là chưa build, chưa test được**; không báo "đã build".
- Linker script: section NOLOAD (`.bss`, `._heap`) phải có `AT > RAM`, không thì Renode nạp số 0 đè lên flash.
- gcc 9 với ASan trong WSL treo vô hạn nếu không chạy qua `setarch x86_64 -R`.
- PlatformIO và CMake build cùng một danh sách file: thêm file nguồn thì sửa **cả** `platformio.ini` **và**
  `CMakeLists.txt` / `src/port/*/port.cmake`. File trong `src/app`, `src/demo` tự vào build của PlatformIO; `src/demo`
  và `src/remote` cũng tự vào CMake (`file(GLOB)`, `AK_APP`), còn `src/app` trong CMake và `src/port/web/build.py` thì
  phải khai.
- Trong `platformio.ini`: `-I…` tính từ gốc repo (`-Isrc/kernel/inc`), còn `build_src_filter` tính từ `src_dir = src`.

## 4. Kernel AK: luật không được phá

1. **ISR chỉ làm hai việc:** đẩy byte vào ring buffer, hoặc post message (giữa `task_entry_interrupt()` và
   `task_exit_interrupt()`). Mọi xử lý nằm trong task.
2. **Handler chạy tới khi xong rồi return.** Không vòng chờ, không delay dài. Cần chờ thì `timer_set()` rồi xử lý ở
   message sau. `task_system` coi một handler chạy quá 3 giây là treo và reset board (watchdog 8 giây).
3. **Task nói chuyện với nhau bằng message.** Không gọi chéo hàm của task khác, không dùng chung biến ghi từ hai nơi.
4. **Việc định kỳ có thể chạy quá chu kỳ thì dùng timer một lần, đặt lại ở cuối handler.** `TIMER_PERIODIC` sẽ dồn
   message tới khi pool cạn (FATAL). Xem `task_ui`: khung 50 ms.
5. **Pool nhỏ:** pure 32, common 8 (tối đa 64 byte), dynamic 8, timer 16. Không post dồn dập; kiểm chỗ còn trống
   trước khi post từ luồng dữ liệu ngoài (xem `task_poll_console`).
6. Signal của ứng dụng bắt đầu từ `AK_USER_DEFINE_SIG` (10); mỗi task một enum riêng trong `src/app/app.h`.

Thêm task: thêm ID vào enum trong `src/app/task_list.h` và một dòng vào `app_task_table` trong `task_list.c` **ở cùng
vị trí** (lệch là FATAL `TK 0x08` lúc khởi động), khai signal trong `app.h`, post message khởi tạo trong `app_init()`.
Ưu tiên: 7 là timer của kernel, 6 là `task_system`; task nghiệp vụ ở dưới.

Thêm lệnh shell: một dòng trong `shell_cmds[]` của `src/app/task_console.c`; hàm nhận phần chữ **sau** tên lệnh.

## 5. Phần cứng và port

- **Mọi `XXX_InitTypeDef` của SPL phải qua `XXX_StructInit()` trước khi gán field.** Thiếu một field là rác stack bị OR
  vào thanh ghi, đổi theo từng bản build, không báo lỗi.
- Bản đồ bộ nhớ: BOOT 12K `0x08000000` · APP 116K `0x08003000` (header ảnh 256 B, vector ở `0x08003100`) · ảnh chờ cài
  ở flash SPI ngoài `0x80000`, bản sao app cũ (quay lui) ở `0xA0000` · EEPROM: `[0,64)` boot_ctrl, `[64,224)` nhật ký
  sự cố, `[224,256)` dành cho dự án. Kho media của demo là flash SPI `[0,0x80000)`: clip từ 0, log thời tiết ở 16
  sector cuối trừ một, chương trình khối ở sector cuối.
- **RAM: chip trên kit báo DEV_ID `0x429` = STM32L151CB-A, 32K** (BOM ghi CBT6, loại 16K). Mặc định build 16K (chạy trên
  cả hai); `ram_size_kb = 32` / `-DAK_RAM_SIZE=32K` cho chip CB-A, bootloader và app cùng giá trị. Bootloader luôn link
  16K. Đã kiểm trên kit 30/10/2026: 32K thật (không lặp địa chỉ), bản demo 32K chạy, stack chưa dùng 24 KB.
  Chưa kiểm: `chip id` còn đọc được sau khi rút nguồn (không phiên SWD) không.
- **Nạp đè bootloader luôn kèm `-rst`.** Ghi bootloader bằng `STM32_Programmer_CLI -d boot.bin 0x08000000` mà không
  reset thì lõi chạy tiếp mã cũ trên flash vừa bị ghi đè → hardfault rác (`pc 0x20000000`) vào nhật ký sự cố, và
  bootloader có thể bắt đầu cài lại BACKUP ngay lần chạy dở đó (30/10/2026).
- Ảnh mới cài xong chạy **thử** (`boot_ctrl.trial`): app tự xác nhận sau `APP_FW_CONFIRM_MS` (30 s) hoặc `fw confirm`;
  không xác nhận sau 3 lần khởi động thì bootloader cài lại ảnh cũ từ BACKUP. `fw rollback` quay lui chủ động.
- **Vector của app chỉ căn 256 B** (`0x08003100`, bản ký `0x08004100`): bảng vector của app phải ≤ 256 B (16 + 45 ngắt
  của L151 = 244 B, vừa). App viết bằng ngôn ngữ khác (Rust: `cortex-m-rt` mặc định 240 ngắt, bảng 1 KB) không link được
  ở địa chỉ này — xem repo `hohoanganh/ak-mcu-rust`. Đổi chip có nhiều ngắt hơn thì phải dời vector hoặc nới header.
- Biến thể ký số (`AK_IMAGE_SIGN=ON` / `image_sign = 1`): bootloader 16K, APP từ `0x08004000`; ảnh phải ký bằng
  `mkimage.py --sign-key`; khoá công khai ở `src/services/fw/fw_pubkey.c`. **Khoá riêng không bao giờ vào repo** (`tools/keys/` bị git bỏ qua): chạy `python tools/ak_sign.py devkey` một lần mỗi clone trước khi build bản ký; lệnh ghi lại `fw_pubkey.c`, đừng commit thay đổi đó trừ khi cặp khoá là của sản phẩm.
- Bootloader chỉ chạy ảnh có CRC đúng: nạp `app.img`, không nạp `.bin` trần. **App và bootloader lấy cùng một bản
  phát hành** (bootloader 1.3.0 đổi bố cục `boot_ctrl`: bootloader cũ không đọc được trạng thái app mới ghi).
- Chân của AK Base Kit 3.0 nằm trong `src/port/stm32l151/port_cfg.h` và `kit.c`, đã đối chiếu schematic
  (`docs/kit/`). LED PB8 sáng khi chân ở mức thấp.
- Thao tác lên board thật (nạp, xoá flash, reset) phải nói rõ sẽ làm gì và được đồng ý trước; board đang có firmware
  thì sao lưu chip trước khi ghi đè.
- Board reset bất thường: đọc lệnh `crash` trước khi đoán.

## 6. Demo và console

- Ngân sách một khung là 50 ms; mỗi trang OLED tốn khoảng 5,5 ms. Vẽ thẳng vào `gfx_fb()` theo byte khi phủ nhiều điểm.
- Màn hình mới: một `ui_screen_t {name, enter, key, frame, leave}` trong `src/demo/scr_*.c`, thêm vào menu, thêm vào
  `test_demo` (test dựng ảnh màn hình trên máy tính: **xem ảnh đó trước khi nạp**).
- Lập trình kéo thả: trang `src/port/web/blocks*.js` dịch khối thành bytecode, máy ảo `src/demo/blk_vm.c` chạy nó
  (bảng lệnh ở đầu `blk_vm.h`). Thêm một khối = thêm lệnh ở **ba** chỗ: `blk_vm.c`, `blocks-compiler.js`, định nghĩa
  khối trong `blocks.js`; rồi `node tests/test_blocks.js --write` để sinh lại `tests/blk_fixtures.h`.
- Trang `docs/play/inside.html` hiện message thật của firmware web (`web_trace()` trong `src/port/web/web_main.c`). Thêm
  task hoặc signal mới thì thêm tên vào `TRACE_NAMES` ở đó và lời giải thích vào hàm `sig()` của `src/port/web/inside.js`;
  thiếu thì trang chỉ hiện "tín hiệu <số>".
- Xem màn hình kit từ xa: `python tools/ak_screen.py --port COMx shot x.png`; mở thẳng một màn hình: lệnh `ui open <tên>`.
- Console TX của app đi qua ring + ngắt; dòng lệnh dồn nhanh hơn tốc độ xử lý thì bị bỏ, không FATAL.

## 7. Quy ước mã

- C99, thụt lề bằng **tab**, `{` cùng dòng. Chú thích và tên trong mã bằng tiếng Anh; tài liệu bằng tiếng Việt.
- Header guard kiểu `__TASK_LIST_H__`, bọc `extern "C"`.
- Build với `-Wall -Wextra -Werror`: không để cảnh báo.
- Không sửa `third_party/`. Cần đổi hành vi thì bọc ở `src/services` hoặc `src/port`.

## 8. Phát hành

Số phiên bản app nằm ở bốn chỗ, đổi cùng lúc: `src/app/app.h`, `src/port/stm32l151/fw_header.c`, `platformio.ini`,
`CMakeLists.txt` (`APP_VERSION`). Bootloader: `src/boot/boot_main.c` và `BOOT_VERSION` trong `CMakeLists.txt`; tăng số
mỗi khi mã bootloader đổi.

Trình tự: cập nhật `CHANGELOG.md`, số phiên bản trong hai README, `docs/index.html` → dựng lại bản web → `make test` →
build sạch boot, app, app_mbmaster, demo → commit, push, chờ CI xanh → tag `ak-mcu-base-v<x.y.z>` → GitHub Release đính
kèm `.img`, `.bin` của bootloader và `.elf`. Ghi chú phát hành nói rõ cái gì đã chạy trên kit, cái gì mới qua test trên
máy tính.

## 9. Git và nội dung

- Nếu có thư mục `_mailbox/` (hộp thư giữa các session, không vào git): khi bắt đầu một lượt làm việc và khi vừa xong
  một việc, đọc `_mailbox/den-ak-kit.md` và xử lý các tin còn `[ ]`; quy ước ở `_mailbox/README.md`.
- Nhánh `main`. Thông điệp commit tiếng Việt, ngắn, nói thay đổi gì và đã kiểm bằng gì.
- Không commit `.pio/`, `build/`, `*.img`, `__pycache__/` (đã có trong `.gitignore`).
- Repo là **cá nhân**: không đưa tên công ty, thông tin liên hệ hay tên sản phẩm thương mại vào mã và tài liệu.
- Ảnh kit trong `docs/kit/` là của AK Foundation (MIT, có ghi nguồn); các sơ đồ SVG sinh từ script trong `tools/`:
  đổi số liệu thì sửa script rồi chạy lại, không sửa tay file SVG.
