# Hướng tối ưu ak-mcu-base sau v1.3.9: đo trước rồi mới chọn

Lập ngày 15/10/2026. Khác hai tài liệu trước ([huong-toi-uu-tiep.md](huong-toi-uu-tiep.md),
[huong-phat-trien.md](huong-phat-trien.md)) vốn dựa vào tra cứu, tài liệu này dựa vào **số đo trên chính mã của base**:
build lại mọi biến thể, đọc bảng ký hiệu, ghi khung hình của demo rồi tính. Mỗi hướng ghi bằng chứng, cách làm, cỡ
việc (S = vài giờ, M = một hai ngày, L = nhiều ngày) và rủi ro. Số nào chưa đo trên kit thì ghi "chưa kiểm chứng".

## Đã làm (15/10/2026, chưa phát hành; thử trên kit 16/10/2026, xem cuối mục)

| Mục | Kết quả | Kiểm bằng |
|---|---|---|
| 1. OLED gửi đoạn cột đổi | `kit_lcd_write(page, x, data, n)`; `gfx_flush()` gửi từ cột đổi đầu tới cột đổi cuối; `ui` in số byte | `test_demo` (test mới), 16 cảnh ghi lại giống từng byte bản cũ, bản web dựng lại |
| 2. Modbus slave không chặn | Chờ đường dây im t3.5 rồi mới đưa khung cho nanoMODBUS; trả lời gửi bằng ngắt TXE/TC (`hal_rs485_send`); COMMIT của OTA chỉ cài sau khi câu trả lời ra hết | `test_modbus` (2 test mới: khung đến dần, trả lời không chặn) |
| 3. Vùng nhớ chung màn hình | `ui_scratch()` 320 B: thân rắn (Snake), lưới Life và sao (Saver) | `test_demo`, khung giống từng byte |
| 5. Kernel | `task_post_pure_msg_try()` / `task_post_common_msg_try()`; `AK_DYNAMIC_MSG_POOL_SIZE=0` bỏ hẳn message động và malloc/free, các app của base dùng nó | `test_kernel` (2 test mới), `test_kernel_nodyn` (cùng test, không message động) |
| 7. CI và vòng test | `AK_APP=demo/kit_tools/remote` trong CMake, `make stm32-all`, CI build tất cả và kiểm RAM heap + stack ≥ 3 KB; `make test-fast` (18 s thay cho 76 s) | build sạch `-Werror` mọi bản |

Số mới (gcc 13.2.1): app 21.056 B flash (−12 B dù thêm mã RS485, nhờ bỏ malloc/free), RAM heap + stack của
demo 8.724 B (trước 8.236), kit_tools 4.060 B (trước 3.572), remote 11.104 B.

### Thử trên kit (16/10/2026)

AK Base Kit, bản `demo` của commit `6dc035a`, nạp qua UART (`ak_fw.py`). Thời gian khung do lệnh `ui` báo (thời gian
vẽ và gửi một khung), trung bình 20 mẫu cách nhau 0,25 s, màn hình tự chơi; bản cũ là bản `main` trước đó (tự báo 1.3.8,
gửi cả trang):

| Màn hình | Bản cũ (ms) | Bản mới (ms) | Byte gửi mỗi khung (mới) |
|---|---|---|---|
| Snake | 3,8 | 1,4 | 5 |
| Tetris | 15,4 | 3,8 | 15 |
| Breakout | 8,8 | 1,6 | 4 |
| Invaders | 15,5 | 3,7 | 32 |
| Flappy | 35,0 | 16,9 | 360 |
| Dino | 21,4 | 9,5 | 186 |
| Pong RS485 | 13,8 | 2,6 | 29 |
| Khối 3D | 28,6 | 8,9 | 107 |
| Mê cung 3D | 31,8 | 19,4 | 381 |
| System monitor | 15,7 | 8,3 | 73 |

Modbus slave và OTA qua RS485 (bộ chuyển USB–RS485, 9600 baud): đọc/ghi và exception 2 đúng; master hỏi liên tục
(ghi rồi đọc 4 thanh ghi, khoảng 70 ms một cặp) trong 30 s ở Tetris và 30 s ở khối 3D: 841 cặp, 0 lỗi, thời gian khung
như khi không có tải (3,7 → 3,9 ms; 8,9 → 8,7 ms). OTA 58 KB qua RS485 mất 78 s, kit tự khởi động lại vào ảnh mới;
ngắt giữa chừng thì firmware cũ chạy tiếp. Pong với `tools/pong_peer.py` chơi được. Nhật ký sự cố trống sau toàn bộ
lượt thử, mức stack còn trống 7,7 KB. Lệnh `ui` không tính thời gian `mb_slave_poll()` nên chưa đo trực tiếp được phần
CPU mà Modbus trả lại; số trên chỉ cho thấy tải Modbus không làm chậm khung. Bản `app` (không demo) và `app_mbmaster`
chưa thử trên kit.

Chưa làm và vì sao: I2C OLED 400 kHz (cần máy phân tích logic và kit), gửi RS485 bằng DMA (đã dùng ngắt TXE thay
vì DMA: ở 9600 baud chỉ khoảng 1.000 ngắt/giây, khỏi phải kiểm bảng kênh DMA trên kit), master/gateway thành máy
trạng thái, vào bằng sự kiện → tickless → Stop, nén/delta OTA, gom ba danh sách file về một (CMake đã tự lấy cả thư
mục `src/demo`, `src/remote`; `src/app` và `build.py` vẫn khai tay).

Cách đo:

- arm-none-eabi-gcc 13.2.1 (xPack), cờ y như `platformio.ini` / `port.cmake` (`-Os`, LTO, newlib-nano).
- `boot`, `app`: `make stm32`. `demo`, `kit_tools`, `remote`: dựng tay cùng danh sách file và cờ của env PlatformIO
  tương ứng (PlatformIO không tải được platform từ môi trường đo). Khác duy nhất: SPL cũng đi qua LTO, nên con số có
  thể lệch vài chục byte so với `pio run`.
- Thời gian bus OLED: `build/host/test_demo <thư mục> record` (10.754 khung, 16 cảnh) rồi
  `python tools/oled_bus_cost.py <thư mục>`, quy đổi theo số đo cũ trên kit: **5,5 ms một trang**.
- `make test` 7/7 trước khi đo. **Chưa chạy gì trên kit trong lượt này.**

## 0. Hiện trạng: nút thắt không còn là flash

| Bản build | Flash dùng / vùng | RAM tĩnh (.noinit+.data+.bss) | Còn cho heap + stack |
|---|---|---|---|
| boot | 9.160 / 12.288 B (ngân sách CI 10.240) | 760 B | 15,6 KB |
| app (Modbus slave) | 21.068 / 118.784 B | 3.232 B | 12,8 KB |
| demo | 58.796 B | 8.160 B | 8,0 KB |
| kit_tools | 64.528 B | 12.824 B | **3,5 KB** (stack dành sẵn 2 KB) |
| remote | 30.732 / 53.248 B | 5.412 B | 10,7 KB |

Đọc bảng:

1. **Flash dư nhiều** ở mọi bản app. Không đáng bỏ công ép thêm cờ biên dịch hay viết lại SPL. Chỗ duy nhất chật là
   bootloader: còn 1.080 B tới ngân sách CI. Mọi tính năng mới nên tránh đụng bootloader (xem mục 6).
2. **Thời gian mới là nút thắt**: hai chỗ giữ CPU hàng chục tới hàng trăm ms trong một lượt, ngược với luật "handler
   chạy tới khi xong" của kernel: đẩy OLED bằng I2C bit-bang (mục 1) và Modbus RTU đọc/ghi kiểu chờ (mục 2).
3. **RAM chỉ chật ở `kit_tools`** (bộ đệm bắt SPI 5,6 KB). Bản demo còn 8 KB nhưng mỗi màn hình giữ biến riêng suốt
   đời chương trình (mục 3).
4. Kernel đã gọn (khoảng 3,2 KB mã: task 1.353 B, message 1.194 B, timer 707 B khi build không LTO); đường điều phối
   message ngắn. Tối ưu vi mô ở đây được rất ít (mục 5).

## 1. OLED: chỉ gửi đoạn cột đã đổi (S, lợi nhất cho demo)

**Bằng chứng.** `gfx_flush()` so khung với bản sao `shown[]` và gửi **nguyên trang 128 cột** nếu trang có đổi dù chỉ
một byte. Một trang trên bus là 135 byte I2C (5 byte lệnh vị trí, 2 byte địa chỉ/điều khiển, 128 byte dữ liệu), đo
được 5,5 ms; 8 trang là 44 ms trên khung 50 ms. Tính trên các khung ghi từ `test_demo`, nếu mỗi trang chỉ gửi từ cột
đổi đầu tiên tới cột đổi cuối cùng:

| Cảnh | Bus/khung hiện nay | Chỉ đoạn cột đổi | Giảm |
|---|---|---|---|
| tour (menu, đồng hồ, các game) | 15,2 ms | 6,4 ms | 58 % |
| pong | 9,2 ms | 1,6 ms | 83 % |
| tetris | 11,5 ms | 1,2 ms | 89 % |
| breakout | 8,0 ms | 0,7 ms | 91 % |
| 3d (khối lập phương) | 24,5 ms | 4,8 ms | 81 % |
| video | 21,7 ms | 3,3 ms | 85 % |
| flappy | 34,9 ms | 18,8 ms | 46 % |
| maze | 30,8 ms | 17,6 ms | 43 % |
| saver | 32,5 ms | 20,9 ms | 36 % |
| scope | 33,0 ms | 18,2 ms | 45 % |

Bảng đủ 16 cảnh: chạy lại `tools/oled_bus_cost.py`. Khung tệ nhất (đổi màn hình, cả 8 trang) không đổi, vẫn 44 ms.

**Cách làm.**

- `kit_lcd_write(page, x0, data, n)` (đã làm, thay `kit_lcd_write_page`): SSD1309 ở chế độ page addressing đã nhận cột bắt đầu bằng hai lệnh
  `0x00 | thấp`, `0x10 | cao` (đang gửi sẵn trong `pos[]`), chỉ cần cộng `x0` vào `KIT_LCD_COL_OFFSET` và gửi `n` byte.
- `gfx_flush()` tìm `x0`, `x1` của từng trang khi so với `shown[]` (vẫn một lượt `memcmp` theo byte), chép đúng đoạn
  đó vào `shown[]`.
- Sửa cùng lúc ba bản cài đặt của hàm ghi: `port/stm32l151/kit.c`, `port/web/web_main.c`,
  `tests/test_demo.c` (bản `remote` dùng chung `kit.c`). Thêm test: khung chỉ đổi một điểm thì bus nhận 7 + 1 byte.
- Không bỏ được `shown[]` (1 KB): màn hình nào cũng `gfx_clear()` rồi vẽ lại cả khung, nên đánh dấu "bẩn" trong các
  hàm vẽ sẽ coi mọi thứ là đổi. So với bản sao vẫn là cách rẻ nhất.

**Bước thêm: I2C đúng 400 kHz.** 135 byte × 9 bit trong 5,5 ms là khoảng 220 kbit/s, tức `i2c_delay()` đang chậm
gần gấp đôi chuẩn Fast-mode. Ở 400 kHz một trang còn khoảng 3 ms (−45 %). Phải đo bằng máy phân tích logic và lệnh `ui`
("last frame N ms") sau khi chỉnh; mức tối đa của SSD1309 **chưa kiểm chứng** với datasheet, không vượt 400 kHz.
Gộp hai bước, flappy ước còn khoảng 10 ms/khung, tour khoảng 3,5 ms: đủ chỗ cho 30 khung/giây ở các game nhẹ.

**Rủi ro.** Thấp. Bản web và `test_demo` hiện coi một lần ghi là cả trang, cần sửa theo. Số "ms" ở trên là quy đổi từ
số byte, chưa đo trên kit.

## 2. Modbus RTU không chặn (M, đúng luật kernel hơn)

**Bằng chứng.**

- Gửi: `hal_rs485_write()` (`port/stm32l151/rs485.c`) chờ cờ TXE từng byte rồi chờ TC. Ở 9600 baud 8N1 một byte là
  1,04 ms: trả lời FC03 đọc 125 thanh ghi (255 byte) giữ CPU **266 ms**.
- Nhận: `mb_slave` đặt `read_timeout = 0` để `poll` không chờ khi đường dây im, nhưng khi đã có byte đầu nanoMODBUS đọc
  phần còn lại từng byte với `byte_timeout` 20 ms (`recv()` → `port_read()` trong `services/modbus/mb_port.c`). Một
  khung OTA FC16 64 thanh ghi (141 byte) giữ CPU khoảng **146 ms** chỉ để nhận.
- Hệ quả trong bản demo: một lần đọc 125 thanh ghi làm đứng khoảng 5 khung OLED; trong bản gateway, socket TCP không
  được phục vụ suốt lúc đó. Bản master (`mb read`) còn chờ tới `APP_MB_RESPONSE_TIMEOUT_MS` (200 ms) trong handler.

**Cách làm, theo thứ tự.**

1. *Nhận cả khung rồi mới đưa nanoMODBUS (S).* Ngắt RX của USART2 ghi `hal_millis()` của byte cuối; HAL thêm
   `hal_rs485_rx_idle_ms()`. `mb_slave_poll()` chỉ gọi `nmbs_server_poll()` khi vòng đệm có dữ liệu **và** đường dây
   đã im quá t3.5 (4 ms ở 9600 baud, 2 ms trên 19200). Lúc đó cả khung nằm trong ring 256 B, `port_read()` lấy liền, không
   chờ. Không sửa `third_party/`.
2. *Gửi bằng DMA (S – M).* DMA1 kênh 7 là USART2_TX trên STM32L1 (theo bảng DMA của RM0038, **chưa kiểm chứng** trên
   kit); ngắt TC nhả chân DE. `port_write()` trả về ngay; `mb_slave_poll()` bỏ qua khi đang gửi (bán song công: lúc
   đang trả lời thì không có yêu cầu mới). Gửi thẳng từ bộ đệm của nanoMODBUS nên không tốn thêm RAM.
3. *Master và gateway thành máy trạng thái (L).* Gửi → `timer_set()` hết hạn trả lời → đọc khung ở message sau → post
   kết quả. Lệnh shell `mb read` in kết quả ở message trả về.

**Kiểm.** Port host đã có RS485 giả: thêm test "khung đến dần từng byte" và "trả lời 255 byte" kiểm rằng mỗi lần
`poll` trả về ngay. Trên kit: lệnh `ui` báo thời gian khung khi PC đọc 125 thanh ghi liên tục.

**Rủi ro.** t3.5 tính theo ms của SysTick nên ở baud cao (trên 19200 chuẩn dùng 1,75 ms cố định) phải làm tròn lên;
khung bị cắt vẫn được nanoMODBUS loại bằng CRC như hiện nay.

## 3. RAM bản demo: vùng nhớ dùng chung cho màn hình (M)

**Bằng chứng** (`.bss` từng file, build không LTO): `scr_saver` 388 B, `scr_snake` 337, `scr_tetris` 59,
`scr_invaders` 46, `scr_music` 46, `scr_pong` 38, `scr_flappy` 28, `scr_breakout` 28, `scr_dino` 26, `scr_maze` 24…
cộng lại khoảng 1.040 B, mà mỗi lúc chỉ một màn hình mở. Máy ảo khối (`blk_vm.c`) giữ thêm 916 B (ảnh chương trình
768 B, biến và stack 128 B) dù chỉ chạy khi màn hình Blocks mở.

**Cách làm.** Một vùng `ui_arena` cỡ bằng màn hình lớn nhất; `enter()` nhận con trỏ vào vùng đó thay cho biến `static`
của file. Không đưa vào vùng chung những gì chạy nền: lịch sử trạm thời tiết (`hist_t`, `hist_h`, 384 B,
`weather_poll()` chạy mỗi khung dù màn hình nào đang mở) và đồ thị `plot`.

**Được bao nhiêu.** Riêng các game: khoảng 650 B. Nếu ảnh chương trình khối cũng vào vùng chung (nạp lại từ flash SPI
bằng `store_load()` lúc mở màn hình, như `blocks_enter()` đã làm khi RAM trống): khoảng 1 KB. Với `kit_tools` (còn
3,5 KB cho heap + stack) đây là chỗ thở đáng kể.

**Rủi ro.** Đường nạp chương trình khối từ máy tính đang ghi vào `blk_buffer()` lúc màn hình khác mở: phải ghi thẳng
xuống flash SPI hoặc chuyển sang màn hình Blocks trước. Cần test `test_demo` cho chuyển màn hình liên tục (vùng chung bị
màn hình trước để lại rác).

## 4. Đường tới ngủ sâu: bỏ polling → tickless → Stop (L, theo chuỗi)

[huong-phat-trien.md](huong-phat-trien.md) mục 2.4 đã nêu Stop mode; đọc mã cho thấy nó **chưa làm được ngay** vì một
lý do cụ thể: SysTick đánh thức CPU 1.000 lần/giây, và mỗi lần thức `task_run()` chạy mọi task polling
(`task_poll_console`, `task_poll_modbus`, `task_poll_buttons`). Các task đó cần được gọi đều mới đúng; tắt tick là
chúng ngừng. Thứ tự đúng:

1. *Vào bằng sự kiện (M).* Ngắt RX console post message khi gặp hết dòng hoặc ring đầy nửa; Modbus theo mục 2 (cuối
   khung là một sự kiện); nút bấm dùng EXTI hoặc timer 10 ms chỉ khi màn hình cần. Mục tiêu: bảng polling rỗng. Hợp với
   luật 1 của kernel (ISR post message).
2. *Idle không tick (M).* Kernel đã có sẵn hai thứ cần: `timer.c` biết `next_expire`, và `timer_tick(t)` nhận số ms đã
   trôi. Thêm `timer_next_expire_ms()`, port đặt hẹn giờ đánh thức rồi gọi `timer_tick(elapsed)` khi thức. Base hiện
   chưa dùng LSE (giờ thực lấy từ PCF85063 ngoài); có thạch anh 32,768 kHz ở PC14/PC15 hay không thì phải đối chiếu
   schematic trong `docs/kit/`. Không có thì RTC chạy LSI khoảng 37 kHz, sai số lớn (**chưa kiểm chứng**).
   `hal_millis()` phải được bù.
3. *Stop mode (M).* Như mục 2.4 cũ: khôi phục HSE + PLL sau khi thức. USART của L1 không đánh thức được từ Stop, nên
   RS485 slave mất byte đầu (EXTI trên chân RX, master thử lại).

**Khi nào đáng làm.** Chỉ khi có sản phẩm chạy pin. Bước 1 thì đáng làm sớm hơn vì nó cũng gỡ việc polling mỗi ms.

## 5. Kernel và pool: ít việc, nhưng có vài chỗ rõ ràng (S)

- **Message động không ai dùng.** Không có lời gọi `task_post_dynamic_msg()` nào trong base, nhưng `msg_free()` tham
  chiếu `free()` nên newlib kéo vào `_free_r`, khoá malloc, `_sbrk`. Thử bỏ đường đó: app **−232 B flash**; bỏ cả pool
  (128 B) và bộ đếm thì thêm khoảng 140 B RAM. Làm bằng `#if AK_DYNAMIC_MSG_POOL_SIZE > 0` trong `message.c`/`task.c`,
  mặc định giữ nguyên để không vỡ dự án đã xuất.
- **Pool common chỉ có một người dùng:** dòng lệnh console (`CONSOLE_SIG_LINE`). 8 × 76 B = 608 B. Đọc `stat` trên kit
  (`used_max`) rồi để dự án tự đặt `AK_COMMON_MSG_POOL_SIZE` (macro đã có `#ifndef`); ghi cách đó vào `docs/porting.md`.
- **Post không FATAL.** Luật 5 bắt người gọi tự kiểm chỗ trống trước khi post từ dữ liệu ngoài. Thêm
  `task_post_pure_msg_try()` / `..._common_msg_try()` trả về 0 khi pool cạn sẽ gọn và khó sai hơn (giữ bản FATAL cho
  đường nội bộ). Cần test trong `test_kernel`.
- **Không đáng làm:** danh sách timer có thứ tự (16 timer, quét tuyến tính là đủ), bỏ chép `current_active_object`
  (8 byte mỗi lần điều phối).

## 6. Cập nhật firmware: làm mọi thứ mới ở phía app, giữ bootloader

**Bằng chứng.** Bootloader còn 1.080 B tới ngân sách; bộ giải nén heatshrink khoảng 1 KB (theo tài liệu trước). Đổi
bootloader thì thiết bị ngoài hiện trường phải nạp ST-Link (bài học bootloader 1.3.0).

**Cách làm.** Nén và delta xử lý **trong app**: app nhận luồng nén/delta qua UART hoặc RS485, dựng ảnh thường vào
STAGING, bootloader vẫn chép ảnh thường như nay và vẫn kiểm CRC/chữ ký. Với delta, ảnh gốc chính là APP đang chạy
trong flash trong, app đọc được trực tiếp. Không đổi bootloader, không đổi định dạng `boot_ctrl`.

**Phụ, ưu tiên thấp:** khối CRC phần cứng của L1 (CRC-32 cùng đa thức, khác chiều bit; đảo bằng lệnh `RBIT` của
Cortex-M3) thay cho bảng 4 bit ở `common/crc.c`. Ước chỉ tiết kiệm vài chục ms lúc khởi động với ảnh 20 – 60 KB
(**chưa đo**), không đáng nếu phải sửa bootloader.

## 7. Quy trình: CI và vòng sửa–test (S)

- **CI không build bản demo nào cho ARM.** Job `stm32-build` chạy `make stm32 stm32-internal stm32-sign stm32-gateway`;
  `demo`, `kit_tools`, `remote`, `app_mbmaster` chỉ có trong PlatformIO và chỉ được dựng trên máy người sửa. Lỗi tràn
  RAM của `kit_tools` (còn 3,5 KB) hay `remote` vượt 52 KB sẽ không ai thấy trước khi nạp. Thêm `-DAK_DEMO=ON` /
  `AK_APP=remote` vào CMake và một bước "ngân sách RAM": heap + stack còn lại tối thiểu, ví dụ 3 KB.
- **Ba danh sách file phải sửa tay cho khớp** (`platformio.ini`, `port.cmake`, `port/web/build.py`). Gom về một
  danh sách (ví dụ `src/sources.txt` theo nhóm) mà `tools/pio_mcu_base.py`, CMake và `build.py` cùng đọc: bớt hẳn một
  loại lỗi mà CLAUDE.md đang phải nhắc.
- **`make test` mất 67 s, trong đó `test_fw` 60 s** (mô phỏng cắt điện 10.081 điểm). Tách nhãn ctest `fast` / `full`:
  máy người sửa chạy `fast` (vài giây), CI chạy `full`.

## 8. Thứ tự đề xuất

| # | Việc | Cỡ | Vì sao trước |
|---|---|---|---|
| 1 | OLED gửi đoạn cột đổi (mục 1) | S | Số đo rõ nhất: 36 – 91 % thời gian bus, ít rủi ro |
| 2 | CI build demo/kit_tools/remote + ngân sách RAM (mục 7) | S | Bảo vệ mọi việc sau |
| 3 | Modbus: nhận cả khung theo t3.5, gửi DMA (mục 2, bước 1 – 2) | S – M | Gỡ chỗ chặn 150 – 270 ms |
| 4 | Dynamic message tuỳ chọn, post `_try` (mục 5) | S | Nhỏ, dọn đường cho dự án |
| 5 | I2C OLED đúng 400 kHz (mục 1) | S | Cần máy phân tích logic và kit |
| 6 | Vùng nhớ chung cho màn hình (mục 3) | M | Khi `kit_tools` hoặc demo cần thêm RAM |
| 7 | Bỏ polling → tickless → Stop (mục 4) | L | Khi có sản phẩm chạy pin |
| 8 | Nén/delta OTA phía app (mục 6) | M – L | Khi OTA qua RS485 chậm thành vấn đề thật |

## 9. Không nên làm

- Ép flash thêm bằng cờ biên dịch hay thay SPL: flash dư hơn một nửa ở mọi bản app, và `third_party/` không được sửa.
- Thêm RTOS để "giải" chỗ chặn của Modbus/OLED: gốc của chúng là I/O kiểu chờ, sửa I/O rẻ hơn và giữ được mô hình
  Active Object.
- Bỏ bản sao `shown[]` của OLED: xem mục 1.
