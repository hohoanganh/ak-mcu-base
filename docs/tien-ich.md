# Tiện ích của ak-mcu-base (từ v1.2.0)

Các tiện ích dưới đây nằm sẵn trong base, đã kiểm trên AK Base Kit ngày 06/10/2026.

| Tiện ích | Dùng để | Lệnh / file |
|---|---|---|
| [Tạo dự án mới](#1-tạo-dự-án-mới) | Xuất một dự án độc lập, không phụ thuộc repo base | `tools/new_project.py` |
| [Nhật ký sự cố](#2-nhật-ký-sự-cố-crash-log) | Biết board ngoài hiện trường reset vì đâu | shell `crash` |
| [Giám sát task](#3-giám-sát-task) | Phát hiện task treo hoặc bị bỏ đói | `app/task_system.c` |
| [Mức dùng stack](#4-mức-dùng-stack) | Biết còn bao nhiêu RAM chưa từng dùng | shell `stat` |
| [Build PlatformIO](#5-build) | Build và nạp như các dự án khác | `pio run` |
| Modbus RTU / RS485 | Slave, master, OTA qua RS485 | [modbus.md](modbus.md) |
| Demo trên kit | Đồng hồ số, Snake, Flappy trên OLED | [demo-kit.md](demo-kit.md) |
| [CI](#6-ci-trên-github) | Tự chạy test và build mỗi lần push | `.github/workflows/ak-mcu-base.yml` |

---

## 1. Tạo dự án mới

`ak-mcu-base` mượn SPL/CMSIS và file board của repo cha. Dự án sản phẩm không nên phụ thuộc cây đó, nên dùng script
để xuất ra một thư mục tự chứa (khoảng 3 MB):

```bash
python tools/new_project.py D:/work/ten-san-pham --board ten-board
cd D:/work/ten-san-pham
pio run                 # app
pio run -e boot         # bootloader
```

- `--board` là tên board ghi trong header ảnh (tối đa 15 ký tự). Bootloader từ chối ảnh mang tên board khác, nên
  **mỗi dòng sản phẩm đặt một tên riêng** để không nạp nhầm firmware giữa các sản phẩm.
- Script chép base, phần SPL/CMSIS mà build cần (`third_party/stm32l1/`), file board, và ghi `BASE_VERSION`
  (tag/commit của base lúc xuất). Thư mục đích phải chưa có hoặc đang trống; base không bị sửa gì.
- Sau khi xuất: `git init`, commit mốc "clean base", rồi mới viết code sản phẩm trong `app/`.

## 2. Nhật ký sự cố (crash log)

Tám bản ghi gần nhất nằm trong EEPROM (NVM offset 64, mỗi bản 20 B, có CRC), còn nguyên sau khi mất điện.
Chỉ ghi khi có sự cố, nên khởi động bình thường không làm mòn EEPROM.

| Loại | Ghi lại |
|---|---|
| `hardfault` | PC, LR, CFSR, task và signal đang xử lý |
| `fatal` | Tag và mã của `FATAL()`, task và signal đang xử lý |
| `watchdog` | Task và signal đang chạy lúc watchdog cắn (handler nào bị treo) |
| `task stalled` | Task không được chạy trong 3 nhịp heartbeat (xem mục 3) |

```
> crash
#0 watchdog     task 2 sig 11
#1 task stalled task 2 got no CPU time
#2 fatal        task 2 sig 11  TEST 0x55
#3 hardfault    task 2 sig 11  pc 0xFFFFFFFE lr 0x080056C5 cfsr 0x00000001
> crash clear
```

Cách hoạt động: trong handler lỗi **không ghi EEPROM**. Port giữ thông tin trong RAM `.noinit` (sống qua reset), lần
khởi động sau `crash_log_capture()` mới chép vào NVM. Kernel báo cho port mỗi lần sắp chạy một handler
(`ak_port_note_dispatch`), nhờ vậy reset do watchdog cũng chỉ ra được handler đang chạy.

Tra `pc`/`lr` ra dòng mã: `arm-none-eabi-addr2line -e firmware.elf 0x080056C5`.

Lệnh thử `crash test fault|fatal|hang|starve` gây ra từng loại sự cố để kiểm trên board. Sản phẩm thật đặt
`-DAPP_CRASH_TEST=0` để bỏ các lệnh này.

Kênh khác (Modbus, RF…) đọc nhật ký bằng `crash_log_count()` và `crash_log_read(n, &rec)` trong
`services/sys/crash_log.h`.

> Cần bootloader từ 1.2.0. Bootloader cũ hơn xoá mất thông tin HardFault trước khi app kịp đọc.

## 2b. Log bản ghi trên flash (`services/sys/flog.c`)

Vòng ghi bản ghi cố định trên flash xoá theo sector (flash SPI của kit, hoặc flash trong qua ba hàm
read / write / erase bất kỳ). Dùng cho số liệu đo, hộp đen, lịch sử sự kiện: ghi nối tiếp, không cần
chỉ mục trong RAM, đọc lại theo thứ tự thời gian, chịu mất điện.

- Mỗi sector mở đầu bằng header `[magic][seq][~seq]`; sector có `seq` lớn nhất là sector đang ghi.
  Mỗi bản ghi là `[dữ liệu rec_size byte][CRC16]`; bản ghi bị cắt điện giữa chừng thì sai CRC và
  `flog_read()` trả 0 (bỏ qua), ô sau nó vẫn trắng nên log ghi tiếp bình thường. Header bị cắt thì
  sector được xoá lại ở lần ghi sau. Hết sector thì xoá sector cũ nhất và ghi tiếp (vòng tròn).
- API: `flog_mount()` (đọc flash dựng lại trạng thái), `flog_append()`, `flog_count()`,
  `flog_read(i)` (0 = cũ nhất), `flog_clear()`. Bản ghi tối đa 60 byte.
- Ví dụ: trạm thời tiết của demo ghi mẫu 1 phút / 15 phút vào 16 sector cuối kho media
  (`demo/scr_weather.c`), nạp lại đồ thị sau reset, `th log` in ra.
- Test: `test_fw` ghi đầy vòng nhiều lần, cắt điện ở **từng** thao tác ghi/xoá (296 điểm) rồi mount lại:
  thứ tự luôn đúng, mất nhiều nhất bản ghi đang ghi; `test_demo` kiểm nạp lại đồ thị sau reset.

## 3. Giám sát task

`task_system` có ưu tiên cao nhất trong các task ứng dụng. Mỗi nhịp heartbeat (1 s) nó:

1. xem task nào đã xử lý ít nhất một message kể từ nhịp trước (`task_alive_take()`),
2. vỗ watchdog cứng,
3. gửi `AK_SIG_PING` cho mọi task (handler bỏ qua signal này là đủ, không cần viết gì thêm).

Hai kiểu hỏng được phân biệt:

| Hiện tượng | Ai phát hiện | Kết quả |
|---|---|---|
| Một handler không bao giờ trả về | Watchdog cứng (8 s), vì `task_system` không chạy được | Bản ghi `watchdog` kèm task và signal bị treo |
| Một task không được cấp CPU (bị task ưu tiên cao hơn chiếm, hàng đợi kẹt) | `task_system`, sau `APP_TASK_STALL_ROUNDS` = 3 nhịp | Bản ghi `task stalled` kèm id task, rồi reset |

Thêm task mới không phải đăng ký gì: mọi task trong `app_task_table` đều được giám sát. Handler hợp lệ chạy lâu
hơn 3 giây sẽ bị coi là treo; việc dài phải cắt nhỏ bằng timer hoặc tự post message cho chính mình.

## 4. Mức dùng stack

Lúc khởi động, vùng RAM trống giữa heap và stack được tô mẫu `0xA5A5A5A5`. Lệnh `stat` in số byte chưa từng bị
đụng tới:

```
> stat
...
stack: 12728 B never used, crash log: 4 record(s)
```

Con số này giảm dần khi stack ăn sâu hơn hoặc heap lớn lên. Theo dõi nó sau khi chạy đủ các tình huống nặng nhất;
còn dưới vài trăm byte là phải xem lại (biến cục bộ lớn, đệ quy, pool message). Hàm: `hal_stack_unused()`.

## 5. Build

| Cách | Lệnh | Ghi chú |
|---|---|---|
| PlatformIO | `pio run -e boot -e app`, nạp bằng `-t upload` | Đã build và chạy trên board (gcc 7.2.1 của PlatformIO, LTO) |
| CMake | `make stm32`, `make test` | Dùng trên Linux/macOS và trên CI |

`pio run` sinh `.pio/build/app/app.img` (ảnh có header, dùng để OTA) và `firmware.elf` đã vá header, nên nạp qua
ST-Link bằng `-t upload` cũng ra ảnh hợp lệ.

## 6. CI trên GitHub

`.github/workflows/ak-mcu-base.yml` chạy khi có thay đổi ngoài `legacy/` và tài liệu:

- **host-tests:** build bằng gcc của máy, chạy unit test kernel, bootloader, crash log và OTA đầu-cuối trên giả lập.
- **stm32-build:** build boot + app ở cả hai kiểu staging bằng `arm-none-eabi-gcc`, kiểm bootloader không vượt
  10.240 B (phân vùng 12.288 B, chừa 2K cho bản sửa sau này), đính kèm `boot.bin`, `app.img`, `.elf`.
