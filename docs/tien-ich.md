# Tiện ích của ak-mcu-base (từ v1.2.0)

Các tiện ích dưới đây nằm sẵn trong base, đã kiểm trên AK Base Kit ngày 06/10/2026.

| Tiện ích | Dùng để | Lệnh / file |
|---|---|---|
| [Tạo dự án mới](#1-tạo-dự-án-mới) | Xuất một dự án độc lập, không phụ thuộc repo base | `tools/new_project.py` |
| [Nhật ký sự cố](#2-nhật-ký-sự-cố-crash-log) | Biết board ngoài hiện trường reset vì đâu | shell `crash` |
| [Giám sát task](#3-giám-sát-task) | Phát hiện task treo hoặc bị bỏ đói | `app/task_system.c` |
| [Mức dùng stack](#4-mức-dùng-stack) | Biết còn bao nhiêu RAM chưa từng dùng; stack xấu nhất tính lúc build | shell `stat`, `tools/stack_report.py` |
| [Build PlatformIO](#5-build) | Build và nạp như các dự án khác | `pio run` |
| Modbus RTU / RS485 | Slave, master, OTA qua RS485 | [modbus.md](modbus.md) |
| Demo trên kit | Đồng hồ số, Snake, Flappy trên OLED | [demo-kit.md](demo-kit.md) |
| [App Android](../tools/android/README.md) | Nạp `.img` và dùng console từ điện thoại qua USB OTG | `tools/android/` |
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

- `--mb-unit 5 --mb-baud 19200 --mb-format 8E1`: mặc định Modbus lúc build của dự án (`APP_MB_UNIT_ID`, `APP_MB_BAUD`,
  `APP_MB_FORMAT` trong `app/app.h`); từng thiết bị vẫn đổi được tại hiện trường bằng `cfg` (mục 2a). Giá trị ngoài
  danh sách bị từ chối trước khi chép gì. Test: `tests/test_new_project.py` (xuất ra thư mục tạm, kiểm các giá trị đã
  ghi, rồi build `test_sys` ngay trong dự án vừa xuất).
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

## 2a. Cấu hình của sản phẩm (`services/sys/settings.c`)

Một bản ghi 32 B ở cuối EEPROM (`[224,256)`): magic, version, len, tới 26 B dữ liệu của app, CRC-16. App mẫu giữ ở đó
địa chỉ Modbus và baud RS485 (`app_cfg_t` trong `app.h`, `app_cfg.c`); dự án thêm trường vào cuối struct và tăng
`APP_CFG_VERSION`, bản ghi cũ trong EEPROM khi đó bị bỏ qua (dùng mặc định), không phải chuyển đổi gì.

```
cfg                 modbus unit 1, baud 9600 8N1, timeout 200 ms (defaults)
cfg unit 7          unit 7, baud 9600 8N1, timeout 200 ms (not saved yet: 'cfg save')
cfg baud 19200
cfg format 8E1      8N1 | 8E1 | 8O1 | 8N2
cfg timeout 300     master / gateway: chờ trả lời 20..5000 ms
cfg save            saved, takes effect after 'reboot'
cfg reset           về mặc định lúc build (APP_MB_*), rồi 'cfg save'
```

`app_cfg_t` vẫn 8 byte: định dạng và thời gian chờ nằm vào ba byte dự trữ vốn luôn bằng 0, và 0 nghĩa là "mặc định"
(8N1, `APP_MB_RESPONSE_TIMEOUT_MS`). Thiết bị đã lưu cấu hình bằng bản trước vẫn đọc được bản ghi cũ, không mất địa chỉ khi
cập nhật firmware.

Cổng RS485 được dựng một lần lúc khởi động nên giá trị mới có hiệu lực sau `reboot`; `mb` in giá trị cổng **đang**
chạy, `cfg` in giá trị đang giữ. Master cũng đặt được qua thanh ghi 32–33 ([modbus.md](modbus.md)). Mất điện giữa lúc
ghi: bản ghi sai CRC, lần sau lên bằng mặc định (một ô, không có bản cũ để lùi: ghi hiếm và do người vận hành làm).
Test: `tests/test_sys.c` (EEPROM trắng 0x00 và 0xFF, lưu rồi đọc, bản ghi khác layout, bản ghi bị cắt, không đè lên
`boot_ctrl` và nhật ký sự cố); `tests/test_sim_cfg.py` chạy lệnh `cfg` trên `ak_sim` qua ba lần khởi động (giá trị sai bị
từ chối, cổng chỉ nhận giá trị mới sau reset, `cfg reset`).

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

### Stack xấu nhất, tính lúc build

`stat` chỉ biết những gì đã chạy. Bản build STM32L151 còn tính **chặn trên** của stack trước khi nạp
(`AK_STACK_REPORT`, mặc định bật cùng LTO): GCC ghi đồ thị lời gọi của mã cuối cùng sau LTO kèm khung stack từng hàm
(`-fcallgraph-info=su`), `tools/stack_report.py` đi hết đồ thị đó sau mỗi lần link:

```
app: worst-case stack
main stack (reset_handler): 1548 B
  reset_handler 64 > task_poll_modbus 360 > ... > hal_nvm_write 32 > FLASH_WaitForLastOperation 16
deepest functions reached through a pointer:
   1196  task_poll_modbus
    644  task_console
    ...
interrupts (+32 B frame each, all nested):
    200  systick_handler
     40  usart2_irq_handler
     ...
worst case: 1852 B
RAM for the stack: 29640 B (_heap_start 0x20000C38 .. _estack 0x20008000), margin 27788 B
```

- **Build hỏng** khi phần dư dưới `AK_STACK_MIN_MARGIN` (mặc định 2048 B), khi có đệ quy (kể cả qua con trỏ hàm), hay
  khi có mảng độ dài thay đổi (VLA). Trình link thì chỉ kiểm vùng stack tối thiểu 2 KB, nên một mảng cục bộ 28 KB
  vẫn link được, còn bước này báo `margin 620 B < 2048 B` (đã thử).
- Ngắt được tính như thể **mọi** ngắt lồng nhau cùng lúc (+32 B khung mỗi lần): chặn trên, thực tế ít tầng hơn.
- Lời gọi qua con trỏ (handler của task, lệnh shell, màn hình demo, hook xuất ký tự, callback của nanoMODBUS…) được
  giải theo bảng `POINTER_TARGETS` trong script: mỗi tệp nguồn có một mẫu tên hàm đích. **Thêm lời gọi qua con trỏ
  ở tệp mới thì thêm một dòng vào bảng đó**; thiếu thì script in `pointer calls without a rule` và tạm tính như thể
  con trỏ tới được mọi hàm bị lấy địa chỉ (có thể ra đệ quy giả).
- Mã của base build với `-Wvla`; nanoMODBUS build với `__STDC_NO_VLA__` (công tắc có sẵn của nó: mảng cố định
  `regs[125]` cho mã hàm 23 thay vì VLA).
- Hàm của newlib (`memcpy`, `strcmp`…) không có số khung, tính 0 B; script liệt kê chúng.

Số đo 03/11/2026 (gcc 13.2.1), stack xấu nhất / RAM còn cho stack: bootloader 1 724 / 15 624 B (bản ký 2 552); app
base 1 852 / 29 640 B; demo 1 760 / 25 080 B; kit_tools 2 000 / 19 976 B; fc 2 092 / 27 192 B. Mọi bản dư trên
13 KB. Trên kit, bản demo đo bằng `stat` còn 24 KB chưa dùng (30/10/2026): khớp với chặn trên.

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
- **android:** client `fw_proto` viết bằng Kotlin (`tools/android/proto`) nạp, cập nhật, xác nhận, quay lui trên
  `ak_sim`, rồi build APK của app Android, đính kèm `ak-flash-apk`.
