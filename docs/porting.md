# Port ak-mcu-base sang chip mới

Code phía trên HAL (kernel, services, boot, app) không đổi. Hai port chip đang có: `port/stm32l151` (AK Base Kit,
SPL) và `port/stm32g071` (NUCLEO-G071RB, viết thẳng trên thanh ghi với header CMSIS, không HAL/LL) — port thứ hai
là bằng chứng lớp HAL đủ tổng quát: **không sửa một dòng nào** phía trên HAL. Một port gồm:

| File | Nội dung |
|---|---|
| `port/<chip>/port_cfg.h` | bản đồ flash, RAM, tên board, chân console/LED |
| `port/<chip>/startup.c` | vector table, `reset_handler`, nhảy app (bản boot), fault handler |
| `port/<chip>/port_<chip>.c` | cài đặt `ak_port.h`, `hal.h`, `hal_flash.h`, `_sbrk` |
| `port/<chip>/boot.ld`, `app.ld` | linker script; `app.ld` đặt `.fw_header` (256 B) ở đầu APP |
| `port/<chip>/fw_header.c` | header ảnh nằm sẵn trong `.elf` của app |
| `port/<chip>/port.cmake` | target `boot`/`app` + bước `mkimage.py patch` sau khi build |

Bắt đầu bằng cách chép `port/stm32l151/` rồi sửa. Thêm nhánh `elseif(AK_PORT STREQUAL "<chip>")`
vào `CMakeLists.txt`.

## Danh sách hàm phải có

**`ak_port.h`** — `ak_port_enter_critical/exit_critical` (cho phép gọi lồng nhau, an toàn trong
ISR), `ak_port_millis`, `ak_port_fatal` (không trả về), `ak_port_idle`.

**`hal.h`** — `hal_init`, `hal_millis`, `hal_delay_ms`, `hal_reset_reason`, `hal_reset`,
`hal_board_name`, `hal_jump_to_app`, `hal_vector_ok`, `hal_console_putc/getc/flush`,
`hal_led_set/toggle`, `hal_wdt_start/kick`, `hal_nvm_read/write`. SysTick (1 ms) gọi
`hal_tick_hook(1)` — app định nghĩa hàm này để chạy timer của kernel.

**`hal_flash.h`** — `hal_flash_info/erase/write/read` theo partition BOOT / APP / STAGING.

## Những điểm cần chú ý

- **Giá trị flash sau khi xóa:** STM32L1 là `0x00`, đa số chip khác là `0xFF`. Khai đúng
  `erased_val`; test host chạy cả hai giá trị.
- **Căn lề vector table:** trên Cortex-M3/M4, VTOR phải căn theo kích thước bảng (lũy thừa 2).
  Header 256 B chỉ đủ khi bảng ≤ 64 vector. Chip nhiều IRQ hơn thì tăng `ALIGN` trong `sections.ld`
  và địa chỉ app trong `fw_image.h` / mkimage cho khớp.
- **Kích thước trang:** `boot_install()` cần trang APP ≥ 256 B và là bội của 128 (buffer chép
  128 B, bằng một half-page của STM32L1). STAGING được phép khác kiểu với APP (ví dụ SPI NOR: sector
  4K, ghi từng byte, xóa ra `0xFF`) nhưng phải lớn hơn hoặc bằng APP. Chip có sector lớn (16–128K,
  ví dụ STM32F4) thì phải chia lại partition theo sector.
- **STAGING trên flash ngoài:** xem `port/stm32l151/spi_nor.c` và cách `hal_flash_*` điều phối theo
  partition. Nếu không phát hiện được chip lúc khởi động thì trả `size = 0` cho STAGING: OTA sẽ báo
  lỗi gọn gàng thay vì ghi bừa.
- **NVM:** chip không có EEPROM thì dùng một trang flash riêng cho `boot_ctrl`. Nên ghi kiểu
  append (nhiều bản ghi trong trang, bản ghi cuối hợp lệ là bản đang dùng) để đỡ hao mòn flash.
- **App không được ghi APP/BOOT:** port STM32L1 chặn ngay trong `hal_flash_erase/write` theo
  `AK_BOOTLOADER`. Port mới nên làm giống vậy.
- **`.noinit` chung giữa boot và app:** đặt ở đầu RAM trong cả hai linker script, dùng để
  truyền yêu cầu nhảy app, nguyên nhân reset và thông tin fault.

## Kiểm port mới

1. `make test` vẫn phải xanh (không phụ thuộc port).
2. Build port mới, xem `--print-memory-usage`, rồi `python3 tools/mkimage.py info build/<chip>/app.img`.
3. Trên board: log boot → `ak_fw.py info` → OTA → rút điện giữa lúc boot đang cài.

## Port thứ hai: STM32G071RB (NUCLEO-G071RB)

`port/stm32g071/`, build bằng `make stm32g0` (`-DAK_PORT=stm32g071 -DAK_MODBUS=none`). Cortex-M0+ 16 MHz (HSI16,
không PLL), flash 128K trang 2K ghi theo double word 8 byte, RAM 36K. Khác port STM32L151 ở những điểm sau, và
chỉ port biết:

| Việc | STM32L151 | STM32G071 |
|---|---|---|
| Thư viện chip | SPL + CMSIS | Chỉ header CMSIS (`third_party/stm32g0`), mã thanh ghi |
| Flash | trang 256 B, ghi word / half-page, xoá về 0x00 | trang 2K, ghi double word, xoá về 0xFF |
| NVM (`hal_nvm`) | EEPROM dữ liệu 4K | **Hai trang flash cuối** luân phiên, bản ghi `[seq][~seq][256 B]`, bản mới hợp lệ là NVM; mỗi lần ghi có thay đổi là xoá + ghi lại một trang (10k lần/trang: đủ cho boot_ctrl và nhật ký sự cố, không dùng để đếm) |
| Bản đồ | BOOT 12K · APP 116K · STAGING, BACKUP trên SPI NOR | BOOT 12K · APP 56K `0x08003000` · STAGING 56K `0x08011000` · NVM 4K `0x0801F000`; không BACKUP (không quay lui), không biến thể ký số |
| Console | USART1 PA9/PA10 | USART2 PA2/PA3 AF1 (cổng COM ảo của ST-Link) |
| LED | PB8, sáng khi thấp | PA5, sáng khi cao |
| RS485 / Modbus | USART2 | chưa có (`AK_MODBUS=none`) |
| HardFault | lưu PC/LR/CFSR | lưu PC/LR; M0+ không có CFSR; stub asm thuần Thumb-1 |

Vector table: `reset_handler` tự đặt `SCB->VTOR` vào bảng của chính ảnh (boot hay app), nên app nạp bằng debugger
không qua boot vẫn chạy.

### Chạy trong Renode

Chưa có board thật; port này được chạy trong **Renode** (`platforms/cpus/stm32g0.repl` của Renode 1.15.3):
`tests/renode/stm32g071.resc` (mở thủ công) và `tests/renode/stm32g071.robot` (3 ca: boot kiểm ảnh rồi khởi động
app, shell `ver`/`info`/`stat` trả lời đúng, không có app thì vào loader). `make renode` (cần `renode-test` của bản
portable trong PATH, hoặc `RENODE_TEST=…`), CI chạy job `stm32g071-renode`.

Hai điều phải biết khi đưa firmware này vào Renode:

- STM32 thật ánh xạ flash tại địa chỉ 0 và lấy SP/PC ở đó sau **mỗi** reset; `stm32g0.repl` không có gì ở 0. Vì
  bootloader khởi động app bằng cách reset (`hal_jump_to_app`), script đặt thêm một vùng nhớ 12K tại 0 và chép
  `boot.bin` vào (vector chứa địa chỉ tuyệt đối trong flash nên chạy tiếp được). Thiếu nó CPU dừng sau lần reset
  đầu tiên.
- Trình nạp ELF của Renode nạp theo địa chỉ nạp (LMA) kể cả phần không có nội dung. Linker script phải ghi rõ
  `AT > RAM` cho `.bss`/`._heap` (NOLOAD); không thì chúng thừa kế vùng nạp flash sau `.data` và Renode đổ số 0
  đè lên vùng APP. Đã sửa cho cả hai port (`sections.ld`), file `.bin`/`.img` không đổi một byte.
- Renode mô hình flash controller theo STM32F4, nên ghi flash (OTA, NVM) **chưa chạy** trong giả lập; đường
  boot → app → shell không ghi flash nên chạy đủ.
