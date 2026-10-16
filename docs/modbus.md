# Modbus RTU trên RS485

Dùng thư viện [nanoMODBUS](https://github.com/debevv/nanoMODBUS) (MIT, `third_party/nanomodbus/`).
Một cổng RS485, một vai trò, chọn lúc build:

| Vai trò | PlatformIO | CMake | Cờ |
|---|---|---|---|
| Slave (mặc định) | `pio run -e app` | `-DAK_MODBUS=slave` | `APP_MODBUS_SLAVE` |
| Master | `pio run -e app_mbmaster` | `-DAK_MODBUS=master` (`make stm32-mbmaster`) | `APP_MODBUS_MASTER` |
| Gateway TCP ↔ RTU | `pio run -e app_gateway` | `-DAK_MODBUS=gateway` (`make stm32-gateway`) | `APP_MODBUS_GATEWAY` |
| Không dùng | — | `-DAK_MODBUS=none` | — |

Cổng: USART2, PA2 TX, PA3 RX, chân hướng PA1 (`port/stm32l151/port_cfg.h`), 8N1.
Tốc độ và địa chỉ: `APP_MB_BAUD` (9600), `APP_MB_UNIT_ID` (1) trong `app/app.h`.

> **Đã kiểm trên AK Base Kit (06/10/2026)** qua bộ chuyển USB-RS485:
> slave đọc/ghi thanh ghi và trả đúng exception; OTA qua RS485 (ảnh 20K mất 26 s, ảnh 27K mất 35 s ở 9600 baud);
> bỏ dở giữa chừng rồi nạp lại; master đọc/ghi một slave giả trên máy tính, nhận đúng exception và timeout.
> Unit test trên máy tính: 174 kiểm tra.

## Slave

App khai thanh ghi thành các khối `uint16_t` của chính nó:

```c
static uint16_t status[3], setpoint[2];

static const mb_reg_block_t holding[] = {
	{ 0,   3, status,   MB_REG_RO },
	{ 100, 2, setpoint, MB_REG_RW },
};
static const mb_slave_map_t map = { holding, 2, 0, 0, on_write };

mb_slave_init(1, 9600, &map);        /* địa chỉ 1 */
mb_slave_enable_ota(on_ota_commit);  /* cho phép cập nhật firmware qua RS485 */
/* gọi mb_slave_poll() trong một polling task */
```

- Mã hàm: 03, 04, 06, 16.
- Yêu cầu phải nằm trọn trong một khối, nếu không trả exception 2. Ghi vào khối chỉ đọc cũng trả exception 2.
- `on_write(address, quantity)` được gọi sau khi master ghi xong.
- `mb_slave_poll()` không bao giờ đứng chờ đường dây. Nó chỉ nhận yêu cầu khi đường dây đã im 3,5 ký tự (5 ms ở
  9600 baud, 2 ms trên 19200), lúc đó cả khung đã nằm trong vòng đệm RX; câu trả lời đi ra bằng ngắt
  (`hal_rs485_send()`), chân hướng nhả khi bit stop cuối ra khỏi dây. Trước bản này, trả lời 125 thanh ghi giữ CPU
  266 ms và nhận một khung OTA giữ khoảng 146 ms. Khung đứt giữa chừng vẫn chờ tối đa 20 ms cho byte còn thiếu.
- Lệnh COMMIT của OTA: app chỉ cho `task_fw` cài và reset sau khi câu trả lời đã ra hết khỏi dây.
- Master và gateway vẫn chờ câu trả lời trong handler (`APP_MB_RESPONSE_TIMEOUT_MS`); đổi sang máy trạng thái là
  việc còn lại (`docs/huong-toi-uu-sau-v1.3.9.md` mục 2).

Bảng thanh ghi của app mẫu:

| Địa chỉ | R/W | Nội dung |
|---|---|---|
| 0 | R | Phiên bản app: `(major << 8) \| minor` |
| 1 | R | Phiên bản app: patch |
| 2 | R | Uptime, giây |
| 3 | R | Số bản ghi sự cố |
| 16–19 | R/W | Thanh ghi thử, giữ tới khi reset |

Lệnh shell `mb` in số byte đã nhận và số request đã trả lời. Có byte mà không có request nào được trả lời thì sai
baud hoặc đảo A/B; không có byte nào thì không có tín hiệu tới bộ thu.

## Cập nhật firmware qua RS485

![Năm bước của một lần cập nhật firmware](diagram-ota.svg)

Khối thanh ghi `0xF000`, cùng địa chỉ và trình tự với base cũ, mang ảnh `.img` của base mới.

| Thanh ghi | R/W | Ý nghĩa |
|---|---|---|
| `F000` | W | Lệnh: 1 BEGIN, 2 COMMIT, 3 ABORT |
| `F001` | R | Trạng thái: 0 rảnh, 1 đang nhận, 2 đã commit; `0x8001` header, `0x8002` offset, `0x8003` CRC ảnh, `0x8004` kích thước |
| `F002–F003` | R/W | Kích thước cả file `.img` (hi, lo) |
| `F004` | R/W | Không dùng với kiểu ảnh này (ghi 0) |
| `F005–F006` | R/W | Kiểu ảnh: `0x57464B41` |
| `F007–F008` | R | Số byte đã nhận |
| `F009` | R | Mã lỗi chi tiết của lần hỏng gần nhất (`fw_err_t`) |
| `F010–F011` | W | Offset của khối, ghi cùng dữ liệu |
| `F012–F051` | W | Dữ liệu khối, tối đa 64 thanh ghi = 128 byte, byte cao trước |

- COMMIT kiểm cả ảnh trong STAGING (CRC header, tên board, địa chỉ nạp, CRC ảnh, bảng vector) rồi mới trả lời.
  Trước đó app đang chạy không bị đụng tới; đứt giữa chừng thì chạy lại từ BEGIN.
- Khối hoặc COMMIT gửi lại vì mất phản hồi đều được chấp nhận, không ghi hai lần.
- Broadcast (địa chỉ 0) không điều khiển được khối này.
- Ảnh kiểu base cũ (`0x1A2B3C4D`) bị từ chối với trạng thái `0x8001`.

Tool trên máy tính (chỉ cần `pyserial`):

```bash
python tools/ak_mb.py --port COM16 info
python tools/ak_mb.py --port COM16 read 16 4
python tools/ak_mb.py --port COM16 write 16 123 456
python tools/ak_mb.py --port COM16 flash .pio/build/app/app.img
```

## Master

```c
mb_master_init(9600, 200);                           /* chờ trả lời tối đa 200 ms */
int err = mb_master_read_holding(5, 0, 4, regs);     /* thiết bị 5, thanh ghi 0..3 */
```

Giá trị trả về: `0` thành công, số dương là mã exception của slave, số âm là lỗi đường truyền (`MB_ERR_TIMEOUT`…).
Mỗi lần gọi chặn trong thời gian khung cộng thời gian chờ trả lời, nên gọi một request cho mỗi message và giữ
timeout ngắn. Lệnh shell: `mb read <unit> <addr> [count]`, `mb write <unit> <addr> <value>`.

## Gateway Modbus TCP ↔ RTU (W5500)

Kit cắm mô-đun **W5500** vào cổng SPI mở rộng J6 (SPI1 dùng chung với flash NOR, chip select PA4 = chân 4 của J6,
`PORT_W5500_CS_*` trong `port_cfg.h`) thành gateway: máy tính / PLC / SCADA nói Modbus TCP với kit, kit hỏi các slave
RS485 bằng RTU rồi trả lời. W5500 chạy trọn TCP/IP trong chip, firmware chỉ chuyển byte qua socket 0.

- **Lớp HAL mới `hal/hal_tcp.h`:** một socket server, một client tại một thời điểm, đọc không chặn. Port STM32L151:
  `port/stm32l151/w5500.c` (khung SPI địa chỉ 16 bit + byte điều khiển, mọi vòng chờ chip đều có giới hạn).
  Port host: hàng đợi trong RAM (`host_tcp_*`) cho unit test.
- **Dịch vụ `services/modbus/mb_gateway.c`:** server nanoMODBUS transport TCP; mỗi yêu cầu gọi `mb_master_*` trên
  RS485. Unit identifier trong MBAP chọn slave RTU (0 = broadcast RTU, chỉ với lệnh ghi). Chuyển tiếp FC 03, 04, 06,
  16; lệnh khác trả exception 1; slave không trả lời (hoặc trả lời sai) → exception 4; exception của slave đi qua
  nguyên vẹn. Client rời đi giữa khung → đóng socket, nghe lại; khung bị cắt quá 50 ms giữa hai byte → bỏ.
- **Cấu hình trong `app/app.h`** (đổi bằng `-D`): `APP_NET_MAC` (locally administered `02:41:4B:00:00:01`),
  `APP_NET_IP` 192.168.1.80, `APP_NET_MASK`, `APP_NET_GW`, `APP_MB_TCP_PORT` 502. Chưa có DHCP.
- `mb_gateway_poll()` chạy trong vòng polling của kernel; trong lúc chờ slave nó chặn tối đa
  `APP_MB_RESPONSE_TIMEOUT_MS` (200 ms) như master. Shell: `mb` in trạng thái (chip, link, client, số yêu cầu, số
  lỗi); `mb read` / `mb write` vẫn dùng được từ console.
- Không có chip (VERSIONR không phải 0x04): log `no Ethernet chip: gateway off`, master RS485 vẫn chạy.

Thử trên máy tính: `test_modbus` có một client Modbus TCP giả gửi FC 3/4/6/16, exception, lệnh không hỗ trợ, slave
im lặng, unit khác, client rớt giữa khung, khung bị cắt (55 kiểm tra). **Chưa thử với mô-đun W5500 thật**: driver
viết theo datasheet W5500, cần kiểm trên kit trước khi dùng.
