---
name: ak-mcu-firmware
description: Viết, sửa và review firmware bare-metal trên ak-mcu-base — kernel AK (Active Object, không RTOS, không preemptive) cho STM32L151 / AK Base Kit. Dùng khi thêm task, message, timer mềm, ISR, lệnh shell, màn hình OLED, cấu hình EEPROM; khi làm Modbus RTU (slave, master, gateway), bootloader, OTA, bản đồ flash; khi board reset bất thường; hoặc khi review trước lúc nạp. Nhận ra qua file BASE_VERSION, tools/new_project.py, task_post_*, timer_set, ak_msg_t, app_task_table, fw_proto, boot_ctrl, app.img, hay các cụm "kernel AK", "AK MCU", "AK Base Kit". Use for ak-mcu-base / AK kernel firmware on STM32L151.
license: MIT
compatibility: ak-mcu-base từ v1.3.11 (bootloader 1.4.0). Không dùng cho legacy/ (base cũ ak-base-kit-pio, có BSF) — quy ước của nó ở legacy/README.md.
metadata:
  repo: https://github.com/hohoanganh/ak-mcu-base
  version: "1.1"
---

# Firmware trên ak-mcu-base (kernel AK)

Kernel AK là **Active Object không preemptive**: mỗi task là một hàm xử lý message, chạy tới khi xong rồi trả về. Tính
đúng đắn của cả firmware dựa vào một bất biến; phá nó là sinh race condition rất khó tìm.

**Xác định đang ở đâu trước khi sửa:**

- **Repo ak-mcu-base** (source base): đọc `CLAUDE.md` ở gốc repo. Nó chi tiết hơn skill này và luôn mới nhất.
- **Dự án xuất từ base** (có file `BASE_VERSION`): mã của dự án nằm ở `src/app/` (màn hình ở `src/demo/`). Lỗi nằm ở phần
  nền (`kernel/`, `services/`, `boot/`, `port/`) thì sửa ở base trước rồi mới đưa xuống dự án, kẻo lỗi quay lại.

## 0. Bất biến

1. **ISR chỉ làm hai việc:** đẩy byte vào ring buffer, hoặc post message (giữa `task_entry_interrupt()` và
   `task_exit_interrupt()`). Không xử lý nghiệp vụ, không `printf`, không gọi HAL chặn.
2. **Handler chạy tới khi xong rồi return.** Không vòng chờ, không delay dài: cần chờ thì `timer_set()` rồi làm tiếp ở
   message sau. Handler quá 3 s bị `task_system` coi là treo; watchdog 8 s reset board.
3. **Task chỉ nói chuyện bằng message.** Không gọi chéo hàm của task khác, không dùng chung biến ghi từ hai nơi. Giữa
   task và ISR vẫn phải bảo vệ (ring buffer, cờ `volatile`, che ngắt khi đọc nhiều byte).
4. **Việc định kỳ có thể chạy quá chu kỳ thì dùng timer một lần, đặt lại cuối handler.** `TIMER_PERIODIC` dồn message
   tới khi pool cạn → FATAL.
5. **Pool nhỏ:** pure 32, common 8 (≤ 64 byte), dynamic 8, timer 16. Không post dồn dập; kiểm chỗ trống trước khi post
   từ luồng dữ liệu bên ngoài.

Mọi hành vi đi qua message nên đều log được. Luồng nghiệp vụ nào không log được thường đang đi tắt ngoài kernel.

## 1. Bản đồ bộ nhớ (STM32L151CB: 128K flash, 32K RAM, 32 MHz)

| Vùng | Địa chỉ | Ghi chú |
|---|---|---|
| Bootloader | `0x08000000`, 12K | `src/boot/`, không phụ thuộc chip |
| App | `0x08003000`, 116K | Header ảnh 256 B; **bảng vector ở `0x08003100`**, chỉ căn 256 B nên bảng phải ≤ 256 B |
| Ảnh chờ cài | flash SPI ngoài `0x80000` | Bản sao app cũ để quay lui ở `0xA0000` |
| EEPROM | `0x08080000` | `[0,64)` boot_ctrl (hai bản ghi luân phiên) · `[64,224)` nhật ký sự cố · `[224,256)` cấu hình |
| RAM | `0x20000000` | Đầu RAM là `.noinit` của bootloader (lệnh nhảy, lý do reset) |

- Biến thể ký số (`AK_IMAGE_SIGN=ON`): bootloader 16K, app từ `0x08004000`.
- 32K RAM là của chip Cat.2 (DEV_ID `0x429`). Chip Cat.1 (`0x416`) chỉ có 16K: build với `ram_size_kb = 16`.
- **Nạp `app.img` (có header) vào `0x08003000`, không nạp `.bin` trần**: bootloader kiểm CRC mỗi lần khởi động.
- **Bootloader và app lấy cùng một bản phát hành.** Nạp đè bootloader qua SWD thì luôn reset ngay sau đó (`-rst`), không
  thì lõi chạy tiếp mã cũ vừa bị ghi đè và để lại hardfault rác trong nhật ký sự cố.

## 2. Thêm task, lệnh shell, màn hình, cấu hình

**Task mới**
1. Thêm ID vào enum trong `src/app/task_list.h` và một dòng vào `app_task_table` (`task_list.c`) **ở cùng vị trí**; lệch
   là `FATAL TK 0x08` lúc khởi động.
2. Signal của ứng dụng bắt đầu từ `AK_USER_DEFINE_SIG` (10); mỗi task một enum riêng trong `src/app/app.h`.
3. Ưu tiên: 7 là timer của kernel, 6 là `task_system`; task nghiệp vụ ở dưới, hiển thị và log thấp nhất.
4. Handler dạng `switch (msg->sig)`, mỗi nhánh chạy tới khi xong. Post message khởi tạo trong `app_init()`.
5. Cần đọc bus liên tục thì dùng polling task (`task_polling_set_ability`), không đặt vòng lặp trong handler.

**Lệnh shell:** một dòng trong `shell_cmds[]` của `src/app/task_console.c`; hàm nhận phần chữ sau tên lệnh.

**Màn hình OLED 128×64:** một `ui_screen_t {name, enter, key, frame, leave}` trong `src/demo/scr_*.c`, thêm vào menu và
vào `tests/test_demo.c`. Ngân sách một khung 50 ms, mỗi trang OLED tốn ~5,5 ms: phủ nhiều điểm thì vẽ thẳng vào `gfx_fb()`
theo byte. `test_demo` dựng ảnh màn hình trên máy tính — **xem ảnh đó trước khi nạp**.

**Cấu hình lưu EEPROM:** thêm trường vào `app_cfg_t` (`src/app/app.h`) và tăng `APP_CFG_VERSION`. Lệnh `cfg` xem, đặt,
lưu; giá trị mới có hiệu lực sau `reboot`.

## 3. Modbus RTU trên RS485 (bán song công)

Chọn vai trò lúc build: `env:app` = slave, `env:app_mbmaster` = master, `env:app_gateway` = gateway Modbus TCP → RTU.

- **Slave:** khai thanh ghi theo khối (`mb_slave_map_t`, RO/RW), `mb_slave_init()`, rồi `mb_slave_poll()` trong polling
  task. Một yêu cầu phải nằm trọn trong một khối, không thì trả exception 2. App mẫu: thanh ghi 32 địa chỉ, 33 baud/100,
  34 định dạng khung (0 8N1, 1 8E1, 2 8O1, 3 8N2), lưu vào cấu hình, có hiệu lực sau reset.
- **Master không chặn:** dựng PDU — *Protocol Data Unit* của Modbus, tức mã hàm + dữ liệu, chưa có địa chỉ và CRC — bằng
  `mb_pdu_read()`, `mb_pdu_write_single()`, `mb_pdu_write_multiple()`; gửi bằng `mb_master_send(unit, pdu, len, done)` rồi
  return. `mb_master_poll()` trong polling task nhặt câu trả lời; callback `done` post message; đọc kết quả bằng
  `mb_master_result()` / `mb_result_regs()`. Không có API chặn: đừng chờ câu trả lời trong handler.
- **OTA qua RS485:** khối thanh ghi `0xF000`, ảnh `.img`: `python tools/ak_mb.py --port <cổng> flash app.img`.
- **Chẩn đoán:** lệnh `mb` in số byte đã nhận và số yêu cầu đã trả lời. Có byte mà 0 yêu cầu: sai baud, sai định dạng
  khung, hoặc **đảo dây A/B** (byte bị đảo bit). 0 byte: không có tín hiệu.
- Hạ chân DE sau cờ truyền xong (`TC`), không phải lúc ring buffer vừa rỗng.

## 4. Bootloader và cập nhật firmware

- Ảnh mới cài xong chạy **thử**: app tự xác nhận sau 30 s (`APP_FW_CONFIRM_MS`) hoặc khi gõ `fw confirm`. Sau 3 lần khởi
  động chưa xác nhận, bootloader cài lại ảnh cũ. `fw rollback` quay lui chủ động.
- Nạp qua cổng console: `python tools/ak_fw.py --port <cổng> flash app.img`. Firmware đang chạy không trả lời lệnh nạp
  thì `ak_fw.py --port <cổng> loader` trước, hoặc nạp qua SWD.
- Giao thức `fw_proto` dùng chung UART với console (khung bắt đầu bằng `0xA5`). Lệnh nhị phân riêng của dự án: gán
  `proto_cfg.ext`, mã lệnh từ `0x40`.
- Header ảnh 256 B (CRC32, tên board, địa chỉ nạp) do `tools/mkimage.py` điền. Tên board đặt bằng `--board` của
  `new_project.py`; bootloader từ chối ảnh mang tên board khác.

## 5. Chẩn đoán trên board thật (console 115200)

| Lệnh | Cho biết |
|---|---|
| `crash` | Nhật ký sự cố: HardFault (pc, lr, cfsr), FATAL (tag + mã), watchdog, task treo. **Đọc trước khi đoán** |
| `stat` | Pool đang dùng, số byte stack chưa từng dùng, số bản ghi sự cố |
| `info`, `verify` | Phân vùng, phiên bản từng ảnh, trạng thái boot_ctrl, lý do reset, chip id |
| `cfg`, `mb` | Cấu hình đang dùng; trạng thái Modbus |

- `python tools/ak_screen.py --port <cổng> shot x.png` chụp đúng bộ đệm khung firmware đã vẽ, để kiểm giao diện từ xa;
  `ui open <tên>` mở thẳng một màn hình.
- Bật nguồn mà báo `reset reason 3` (reset mềm) thường do dây SWD còn cắm: debugger reset chip qua SWD. Muốn thử đường
  bật nguồn thật thì gỡ SWD.
- Đầu dò ST-Link báo `Unable to get core ID` hay `No device connected`: kiểm **phiên bản firmware của đầu dò** trước khi
  nghi board. Firmware cho chip ngủ lâu (WFE/WFI, không có ngắt đều) có thể khoá SWD khi đầu dò không có chân NRST:
  giữ BOOT0 rồi bấm RESET để cứu.

## 6. Checklist review (chạy đủ trước khi nói "OK")

- [ ] ISR có logic nghiệp vụ, `printf`, hay gọi HAL chặn? → chuyển vào task.
- [ ] Handler có vòng chờ hay delay > 1 ms? → timer mềm.
- [ ] `task_post_*` từ luồng dữ liệu ngoài có kiểm pool còn chỗ? Hàng đợi đầy là mất message im lặng.
- [ ] Biến ghi từ cả ISR lẫn task: `volatile`, che ngắt khi đọc/ghi nhiều byte?
- [ ] Ring buffer UART/RS485 có xử lý tràn khi bus nghẽn?
- [ ] Việc dài có cắt nhỏ để không handler nào quá 3 s?
- [ ] Mọi `XXX_InitTypeDef` của SPL qua `XXX_StructInit()` trước khi gán? Thiếu một trường là rác stack bị OR vào thanh
      ghi, đổi theo từng bản build, không báo lỗi.
- [ ] Thêm task: enum và `app_task_table` cùng vị trí?
- [ ] Ghi EEPROM/flash: chịu được mất điện giữa chừng (bản ghi kép, CRC) và xoá theo trang?
- [ ] Bảng vector của app ≤ 256 B?
- [ ] Đổi giao thức: đã sửa cả firmware lẫn tool trong `tools/`?
- [ ] Lỗi nằm ở phần nền? → sửa ở base trước.

## 7. Build và test

```bash
make test                         # test trên máy tính (Linux/WSL), ASan + UBSan. Chưa qua thì chưa xong
make test-fast                    # vài giây, cho lúc đang sửa; trước commit vẫn chạy make test
make sim                          # giả lập bootloader + app trên máy tính, gõ help
pio run -e boot -e app -e demo    # firmware: .pio/build/<env>/app.img; bootloader: .pio/build/boot/firmware.bin
```

- Bản STM32 kiểm **stack xấu nhất** sau mỗi lần link (`tools/stack_report.py`): build hỏng khi dư dưới 2 KB, khi có đệ quy
  hay VLA. Thêm lời gọi qua con trỏ hàm thì khai đích trong `POINTER_TARGETS` của script.
- Build với `-Wall -Wextra -Werror`, C99, thụt lề tab. Không sửa `third_party/`: bọc lại ở `services/` hoặc `port/`.
- PlatformIO, CMake và bản web lấy nguồn theo thư mục: thêm `.c` vào thư mục đã có là cả ba tự nhận; thêm thư mục mới thì
  khai ở cả ba.
- gcc 9 + ASan trong WSL treo nếu không chạy qua `setarch x86_64 -R`.
- Không có toolchain thì nói rõ **chưa build, chưa test**; không báo "đã build".

## 8. Mô-đun Rust (tuỳ chọn, mặc định tắt)

`AK_RUST_PROTO` (tách khung `fw_proto`) và `AK_RUST_MODBUS` (tách yêu cầu Modbus slave) link thư viện tĩnh từ
[ak-mcu-rust](https://github.com/hohoanganh/ak-mcu-rust), cần `cargo`. Giữ `libc` đứng trước `libak_ffi` khi link, không
thì Rust kéo `memcpy` riêng vào (+2,9 KB). Panic của Rust vào nhật ký sự cố với thẻ `RUST` kèm tệp và dòng.

## 9. An toàn với board thật

- Nạp firmware, xoá flash, ghi EEPROM, reset board: **nói rõ sẽ làm gì và chờ người dùng đồng ý**.
- Board đang có firmware thì **dump toàn bộ flash ra file** trước khi nạp đè.
- Khoá riêng để ký ảnh (`tools/keys/`) **không bao giờ vào repo**.
