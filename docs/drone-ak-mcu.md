# Phương án: ak-mcu-base làm tay điều khiển drone và bộ điều khiển bay cho drone cỡ nhỏ

Lập ngày 25/10/2026. Nghiên cứu từ mã hiện có của base (`src/remote`, `port/stm32l151/rf_test.c`, sơ đồ chân kit
3.0 trong `docs/kit/`) và tài liệu công khai của các dự án tương tự. Số nào chưa đo trên kit thì ghi **chưa kiểm
chứng**. Chưa viết dòng mã nào cho phần này.

Cỡ việc: S = vài giờ, M = một hai ngày, L = nhiều ngày.

## 1. Tóm tắt

| Câu hỏi | Trả lời ngắn |
|---|---|
| Kit làm tay điều khiển được không? | **Được, và đã có một nửa**: bản `remote` (kit 2.1) đã phát gói điều khiển cho một drone đồ chơi 2,4 GHz qua nRF24L01+. Thiếu cần gạt analog, giao thức hai chiều và failsafe. |
| STM32L151 (Cortex-M3 32 MHz, không FPU, 16 KB RAM) làm FC được không? | **Được cho drone chổi than (brushed) cỡ 65–100 mm, 1S**, vòng điều khiển 1 kHz. Tiền lệ: firmware Silverware bay H8 mini bằng STM32F030 (Cortex-M0 48 MHz, không FPU) ở vòng 1 ms. Không hợp cho drone không chổi than (brushless) cần vòng 4–8 kHz và DShot. |
| Kernel AK có hợp với FC không? | **Không trực tiếp.** AK không chen ngang (non-preemptive): một handler 15 ms của task khác làm vòng điều khiển trễ 15 ms. Cần một tầng thời gian thực cứng chạy trong ngắt timer, đứng ngoài kernel; AK lo phần còn lại (radio, arm, pin, log, cấu hình, OTA). |
| Nên bắt đầu từ đâu? | Đo trước trên kit (mục 6, P0): thời gian tính một vòng điều khiển và thời gian đọc IMU. Nếu một vòng ≤ 40 % của 1 ms thì làm tiếp; nếu không, chuyển sang chip mạnh hơn (mục 5.4). |

## 2. Những gì base đã có

- **Bản `remote`** (`src/remote/task_remote.c`, `pio run -e remote` / `make stm32-remote`): kit 2.1 (STM32L151C8,
  OLED SSD1306, 3 nút, nRF24L01+ trên bo) phát gói 13 byte mỗi 8 ms, nhảy 5 kênh, có gói ghép cặp (bind) trên kênh 75,
  giả khung kiểu HS6200/XN297 bằng nRF24 (`rf_build()` tự dựng PCF, xáo trộn payload, CRC16). Shell `rc` cho máy
  tính lái thay nút, có bộ canh `rc wd` đưa ga về 0 khi mất lệnh. Đây là **công cụ thử từng bit** của một drone đồ chơi,
  không phải tay lái.
- **Kernel, bootloader, OTA an toàn, nhật ký sự cố**: dùng nguyên cho cả tay điều khiển và FC. OTA có chạy thử và quay
  lui là thứ hiếm FC tự làm nào có.
- **Chạy trên máy tính** (`port/host`, `ak_sim`, bản web): cho phép thử bộ điều khiển với một mô hình vật lý trước khi
  gắn cánh quạt (mục 4.5).
- **Phần cứng kit 3.0 còn trống** (theo `docs/kit/connectors.svg`):
  - J6 (2×5, xếp chân như mô-đun nRF24L01+): CE PA8, CSN PA4, SPI1 PA5/PA6/PA7, IRQ PB1. SPI1 dùng chung với flash NOR.
  - J7 / J9: I2C1 PB6/PB7 (chân I2C phần cứng; base hiện bit-bang RTC trên đó).
  - J4: UART3 PB10/PB11.
  - J13 (2×6): PA0, PA11, PA12, PB5, PB9, PB15, PC14, PC15.

## 3. Tay điều khiển

### 3.1 Phần cứng

| Thứ | Phương án | Ghi chú |
|---|---|---|
| Cần gạt | 2 joystick analog (4 trục) | Kit 3.0 lộ ra rất ít chân ADC (PA0 ở J13; PB1 ở J6 nếu bỏ chân IRQ của nRF24). Cách gọn: **ADC qua I2C** (ADS1115 4 kênh 16 bit) hoặc mô-đun joystick Qwiic cắm J7. Kit 2.1 thì xem lại sơ đồ chân trước. Ánh xạ kênh ADC của từng chân phải đối chiếu datasheet STM32L151 — **chưa kiểm chứng**. |
| Radio | nRF24L01+ (có sẵn) hoặc mô-đun PA+LNA | PA+LNA cho tầm xa hơn, tốn dòng hơn |
| Nút, màn hình | 3 nút + OLED có sẵn | Arm/disarm, chọn chế độ, xem pin và chất lượng liên kết |
| Pin | 1S LiPo hoặc 2×AA | Đo áp bằng ADC qua cầu chia |

### 3.2 Giao thức radio: ba lựa chọn

| Lựa chọn | Ưu | Nhược | Dùng khi |
|---|---|---|---|
| **A. Giao thức riêng trên nRF24 thuần** (Enhanced ShockBurst: tự ACK, tự gửi lại, gói ACK mang dữ liệu) | Đơn giản nhất, nRF24 làm hết; ACK payload cho **telemetry** (pin, RSSI giả bằng tỉ lệ mất gói) ngay | Chỉ nói chuyện với FC của chính mình | Tay điều khiển và FC đều là ak-mcu — **đề xuất mặc định** |
| **B. Bayang** (giả XN297 bằng nRF24 như `rf_build()` đang làm) | Mở, có telemetry; Silverware và module Multiprotocol đều hỗ trợ, nên FC của mình bay được bằng tay điều khiển OpenTX/EdgeTX có module Multiprotocol, và ngược lại | Phải giả XN297 từng bit (đã có mẫu trong base) | Muốn FC tương thích hệ sinh thái drone đồ chơi |
| **C. CRSF qua UART** (FC nhận từ một bộ thu ExpressLRS cắm UART3) | Tầm xa, độ trễ thấp, mọi tay điều khiển ELRS dùng được | Thêm một bộ thu; tay điều khiển ak-mcu không dùng được | FC muốn dùng tay điều khiển thương mại |

Gói đề xuất cho A (32 byte tối đa của nRF24, dùng ~12): số thứ tự, 4 trục 11 bit, công tắc (arm, chế độ, beeper),
CRC của ứng dụng. Gửi 250–500 gói/giây, nhảy kênh theo bảng dẫn từ ID ghép cặp.

### 3.3 Phần mềm trên base

- `task_remote` mới (thay bản thử bit): timer một lần mỗi 2–4 ms đọc cần gạt, đóng gói, gửi; đọc ACK payload để lấy
  telemetry. Giữ đúng luật kernel: không vòng chờ — `rf_send()` hiện quay chờ cờ TX_DS tới 40.000 vòng; bản thật nên
  dùng chân IRQ của nRF24 (PB1) đẩy sự kiện vào message.
- Hiệu chỉnh cần gạt (giữa, biên, vùng chết) lưu vào vùng EEPROM `[224,256)` dành cho dự án.
- **An toàn**: không cho arm khi ga chưa ở 0; giữ nút 3 để disarm ngay (bản `remote` đã có "giữ nút 3 → ga 0");
  mất liên kết thì hiện cảnh báo và bíp.

## 4. Bộ điều khiển bay (FC) cho drone chổi than cỡ nhỏ

### 4.1 Mục tiêu

Drone 4 động cơ chổi than lõi rỗng (716 hoặc 8520), khung 65–100 mm, pin 1S, dưới 50 g. Chế độ **acro** (giữ tốc độ
góc) rồi **angle** (tự cân bằng). Không GPS, không bay ngoài trời trong giai đoạn đầu.

### 4.2 STM32L151 có đủ sức không

| Tham chiếu | Chip | Vòng điều khiển |
|---|---|---|
| Silverware trên H8 mini (bo xanh) | STM32F030, Cortex-M0 48 MHz, không FPU, radio XN297L | 1 ms (`LOOPTIME 1000`), động cơ PWM 16 kHz |
| Naze32 với Baseflight/Cleanflight/Betaflight ≤ 3.2 | STM32F103, Cortex-M3 72 MHz, không FPU | Betaflight bỏ F1 từ bản 3.3 vì thiếu flash và sức tính |
| Crazyflie 2.1 | STM32F405, Cortex-M4F 168 MHz + nRF51822 lo radio | PID xếp tầng (vị trí → vận tốc → góc → tốc độ góc) |
| **AK Base Kit** | **STM32L151, Cortex-M3 32 MHz, không FPU, 128 KB flash, 16 KB RAM** | **cần đo** |

Cortex-M3 có nhân 1 chu kỳ và chia phần cứng, M0 thì không; L151 ở 32 MHz chạy flash với 1 chu kỳ chờ. Ước lượng thô:
sức tính L151 32 MHz ngang cỡ F030 48 MHz mà Silverware dùng — **chưa kiểm chứng**, phải đo (mục 6, P0). Bộ nhớ thì
dư: Silverware vừa trên chip 16–32 KB flash, 4 KB RAM.

Tính thô một vòng: đọc IMU, lọc bổ sung (Mahony), 3 PID, trộn ra 4 động cơ ≈ 200–300 phép float mềm, mỗi phép vài
chục chu kỳ → khoảng 10.000 chu kỳ ≈ 0,3 ms ở 32 MHz, tức 30 % của vòng 1 ms. Nếu sát quá thì chuyển sang số nguyên
cố định Q16 cho vòng tốc độ góc. **Chưa kiểm chứng.**

### 4.3 Kiến trúc phần mềm: hai tầng

```
 ngắt timer 1 kHz (ưu tiên cao nhất)          kernel AK (task thường)
 ┌──────────────────────────────────┐      ┌──────────────────────────────┐
 │ đọc IMU (SPI hoặc I2C + DMA)     │      │ task_radio: nhận gói, ghép   │
 │ lọc, ước lượng góc               │ ◄──  │   cặp, nhảy kênh  ─► setpoint │
 │ PID tốc độ góc (+ góc)           │      │ task_arm: arm/disarm, failsafe│
 │ trộn, ghi 4 PWM                  │ ──►  │ task_batt: đo pin, cắt khi yếu│
 │ failsafe: setpoint quá cũ → tắt  │      │ task_log, shell, OTA, sự cố   │
 └──────────────────────────────────┘      └──────────────────────────────┘
        trao đổi qua một struct có số thứ tự (seqlock), không khoá
```

- **Vì sao không để vòng điều khiển trong một task AK**: kernel không chen ngang. Thời gian chờ của vòng điều khiển
  bằng handler dài nhất của các task khác; ví dụ khung OLED của demo mất 4–19 ms trên kit. Drone mất ổn định khi vòng
  trễ vài ms.
- Đây là **một ngoại lệ có chủ ý với luật 1 của kernel** ("ISR chỉ đẩy byte hoặc post message"), phải ghi rõ trong
  mã và CLAUDE.md của dự án FC: ngắt điều khiển ngắn, thời gian chạy có giới hạn, không gọi API kernel nào ngoài post
  message báo lỗi.
- **Failsafe nằm ở tầng ngắt**, không ở task: nếu setpoint mới nhất cũ hơn 100–200 ms thì giảm ga về 0 và disarm. Task
  có thể bị chậm; ngắt thì không.
- Watchdog phần cứng (đã có, 8 s) quá dài cho drone: thêm kiểm "vòng điều khiển còn chạy" mỗi 50 ms, tắt động cơ
  ngay nếu không.
- Nên bỏ OLED trên FC; bit-bang I2C OLED giữ CPU lâu.

### 4.4 Phần cứng FC

| Khối | Chọn | Ghi chú |
|---|---|---|
| IMU | ICM-42688-P hoặc BMI270 qua **SPI**; MPU6050 qua I2C để thử | Đọc 12–14 byte: SPI vài chục µs; I2C 400 kHz khoảng 0,4 ms — nên dùng I2C1 phần cứng + DMA, không bit-bang |
| Động cơ | 4 MOSFET kênh N mức logic (AO3400, Si2302…) + diode, PWM 16–32 kHz | Ở 32 MHz, PWM 16 kHz có 2.000 bước (11 bit) |
| Timer PWM trên kit 3.0 (để thử) | TIM2: PA0 (CH1), PB10 (CH3), PB11 (CH4); TIM3: PB5 (CH2) hoặc TIM4: PB9 (CH4) | Theo bảng chức năng thay thế của STM32L151 — **phải đối chiếu datasheet trước khi đi dây**. PB10/PB11 là UART3 ở J4, dùng cho PWM thì mất UART3. |
| Radio | nRF24L01+ ở J6 (SPI1, chung với flash NOR: phải chia bus cẩn thận, hoặc tắt flash) | Bo riêng nên tách SPI cho IMU và radio |
| Pin | 1S LiPo, đo áp bằng ADC, cắt ở ~3,3 V/cell | |
| Bo | Giai đoạn đầu: kit 3.0 + dây cắm, **không cánh quạt**. Sau đó bo riêng 20×20 mm hoặc nhỏ hơn | |

### 4.5 Thử trên máy tính trước khi bay

Base đã chạy được mã chip trên máy tính. Thêm một mô hình vật lý đơn giản (vật rắn 6 bậc tự do, lực đẩy tỉ lệ bình
phương PWM, nhiễu IMU, trễ) vào `test_*` trên host: kiểm PID ổn định, failsafe tắt động cơ khi mất gói, mixer đúng
chiều. Sau đó bản web có thể vẽ drone 3D bay theo đúng mã FC, như trang "Bên trong firmware" đang làm với message.

## 5. Rủi ro và cách giảm

1. **Sức tính không đủ** → đo P0 trước khi đầu tư; nếu thiếu thì dùng số nguyên cố định, giảm vòng xuống 500 Hz
   (đủ cho brushed nhưng khó bay hơn), hoặc đổi chip (5.4).
2. **Jitter do kernel** → kiến trúc hai tầng (4.3). Đo jitter bằng DWT->CYCCNT, ghi max vào nhật ký.
3. **Tranh chấp SPI1** (nRF24 và flash NOR trên kit) → khi thử FC thì không dùng flash; bo riêng tách bus.
4. **An toàn**: cánh quạt nhỏ vẫn cắt tay. Thử không cánh quạt, có khung bảo vệ, bay trong nhà, luôn có công tắc
   disarm trên tay điều khiển; tuân thủ quy định bay thiết bị bay không người lái nơi bay.
5. **XN297 giả bằng nRF24 kém ổn định** (diễn đàn Deviation ghi nhận XN297 chỉ "phần nào" tương thích) → với cặp
   ak-mcu–ak-mcu dùng giao thức A trên nRF24 thuần.
6. **Đổi chip.** Base đã có port STM32G071 (Cortex-M0+ 64 MHz, không FPU, 36 KB RAM): sức tính xấp xỉ gấp đôi L151
   theo xung nhịp. Muốn FC "thật" (brushless, DShot, vòng 4–8 kHz) thì nên dùng chip có FPU (STM32G4/F4) — ở mức đó
   Betaflight đã làm tốt; giá trị của FC trên ak-mcu là học, tự chủ mã và tích hợp với hệ sinh thái của base
   (OTA an toàn, nhật ký sự cố, thử trên máy tính).

## 6. Lộ trình đề xuất

| Bước | Việc | Cỡ | Điều kiện qua bước |
|---|---|---|---|
| **P0** | Đo trên kit 3.0: một vòng điều khiển mẫu (Mahony + 3 PID + mixer, float mềm và Q16) đo bằng DWT->CYCCNT; đọc MPU6050 qua I2C1 phần cứng; jitter của ngắt timer 1 kHz khi demo đang chạy | S | Vòng ≤ 40 % của 1 ms, jitter ≤ 50 µs |
| **P1** | Tay điều khiển: joystick qua ADS1115 (J7), giao thức A với ACK payload, hiệu chỉnh cần gạt, arm/disarm, failsafe hiển thị; giữ bản `remote` cũ cho drone đồ chơi | M | Gửi 250 gói/s, mất gói < 1 % trong nhà |
| **P2** | FC trên bàn: kit 3.0 + IMU + 4 MOSFET + 4 động cơ **không cánh quạt**; tầng ngắt 1 kHz; failsafe; mô hình vật lý trên host | M – L | Động cơ phản ứng đúng chiều khi nghiêng kit; mất gói → tắt trong 200 ms |
| **P3** | Bo FC riêng 1S cho khung 65–75 mm (L151 hoặc G071, IMU SPI, nRF24, 4 FET); bay acro rồi angle | L | Treo lơ lửng ổn định trong nhà |
| **P4** | Telemetry đầy đủ, chỉnh PID từ tay điều khiển, nhận CRSF (ELRS), log bay vào flash | M | |

### 6.1 P0: mã đo đã có (25/10/2026), chưa chạy trên kit

- **Thuật toán** ở `src/fc/fc_ctrl.c`, C thuần, không libm: lọc gyro PT1 90 Hz, Mahony, PID góc và tốc độ góc (D trên
  phép đo, lọc D 100 Hz, chặn I), trộn QUADX theo thứ tự Betaflight. Hai bản: float (`fc_f_step`, acro hoặc angle) và
  Q16.16 (`fc_q_step`, acro). `tests/test_fc.c` kiểm chiều trộn, Mahony hội tụ đúng góc và quay đúng chiều gyro, chế
  độ angle kéo về thăng bằng, ga 0 thì tắt động cơ, bản Q16 lệch bản float tối đa 1‰ trên 20.000 bước.
- **Lệnh `fcb`** trong bản `kit_tools` (`port/stm32l151/fc_bench.c`):

| Lệnh | Đo gì | Qua P0 khi |
|---|---|---|
| `fcb` | Chu kỳ CPU của một bước (DWT->CYCCNT), 500 lần mỗi bản: float angle, float acro, Q16 acro | float angle trung bình ≤ 12.800 chu kỳ (40 % của 1 ms) |
| `fcb imu` | Tìm IMU ở 0x68/0x69 trên I2C1 (J7/J9), nhận MPU-6050/6500/9250, ICM-20602/20689, ICM-42688-P; đo 200 lần đọc 14 byte qua I2C bit-bang của kit | Chỉ để biết: FC thật dùng I2C phần cứng hoặc SPI (lệnh in thời gian ước tính) |
| `fcb jit <s> [load]` | Ngắt TIM6 1 kHz ưu tiên cao nhất chạy `s` giây trong lúc demo vẫn chạy; `load` chạy luôn bước float angle trong ngắt. `fcb jit` in kết quả: chu kỳ ngắn nhất/dài nhất, số chu kỳ lệch > 10/50/100 µs, thời gian ngắt dài nhất | chu kỳ dài nhất − 1000 ≤ 50 µs |

Cách thử: nạp bản `kit_tools` (OTA: `ak_fw.py --port COMx flash .../app.img`), rồi trong console: `fcb`; cắm IMU vào J7
(nếu có) rồi `fcb imu`; mở một màn hình game, `fcb jit 30 load`, chờ 30 s, `fcb jit`. Tiện thêm: `ui stream` trong lúc đo
để console cũng bận. Ngắt TIM6 không post message, chỉ ghi biến đếm riêng. RAM của `kit_tools` còn 3.616 B cho
heap + stack (ngân sách CI 3.072 B).

### 6.2 P1: tay điều khiển và bên thu (25/10/2026), chưa chạy trên kit

Hai bản build mới, cùng phần cứng (AK Base Kit 3.0 + mô-đun nRF24L01+ cắm J6):

| Bản | Build | Làm gì |
|---|---|---|
| `rc_tx` | `pio run -e rc_tx` / `make stm32-rc-tx` | Tay điều khiển: cần gạt (ADS1115 trên J7, hoặc lệnh `link stick` từ máy tính), 250 gói/s, nhảy 8 kênh, đọc telemetry trong ACK, arm/disarm, bind, hiệu chỉnh, OLED |
| `rc_rx` | `pio run -e rc_rx` / `make stm32-rc-rx` | Bên thu, nửa radio của FC sau này: bám nhảy kênh, ACK kèm telemetry, arm và failsafe; chưa điều khiển động cơ, OLED và `link` hiện cái động cơ sẽ nhận |

**Mã:**
- `src/rc/rc_link.c`: phần không phụ thuộc chip, gồm định dạng gói (có CRC16), địa chỉ và bảng nhảy kênh suy từ ID, cách
  bên thu bám kênh, arm và failsafe, hiệu chỉnh cần gạt, bản ghi EEPROM `[224, 256)`.
- `tests/test_rc.c` (13.433 kiểm tra) mô phỏng hai đầu nhảy kênh:

| Điều kiện mô phỏng | Gói tới | Khoảng mất dài nhất |
|---|---|---|
| Không mất gói | 100 % | — |
| Mất 5 % mỗi chiều | 90,5 % | 76 ms |
| Mất 20 % / 10 % | 68 % | 148 ms |
| Tay điều khiển gửi trễ 0–3 ms, mất 5 % | 90,6 % | 115 ms |
| Tay điều khiển gửi trễ 0–6 ms, mất 5 % | 85,8 % | 184 ms |
| Sau khi mất sóng hẳn | bắt lại kênh trong ≤ 68 ms | |

  Mọi trường hợp đều dưới ngưỡng failsafe 200 ms.
- `src/rc/rc_tx.c`, `rc_rx.c`, `rc_common.c`: các task. `port/stm32l151/nrf24.c`: driver Enhanced ShockBurst (tự ACK, 2 lần
  gửi lại, payload động, payload trong ACK), không chờ sóng.
- `gfx_flush_step()` mới: đẩy OLED từng 24 byte để task radio không bị chặn quá ~1,3 ms.

**Đấu nối:** mô-đun nRF24L01+ cắm thẳng J6 (CE PA8, CSN PA4); nên hàn thêm tụ 10 µF ở chân nguồn mô-đun. Cần gạt: mô-đun
ADS1115 vào J7 (3V3, GND, SDA PB7, SCL PB6, ADDR nối GND → 0x48); A0 roll, A1 pitch, A2 yaw, A3 ga; hai đầu mỗi chiết áp
nối 3V3 và GND.

**Thử:**
1. Nạp `rc_rx` vào kit B, `rc_tx` vào kit A (OTA: `ak_fw.py --port COMx flash .../app.img`).
2. Kit B chưa ghép thì tự vào chế độ bind. Trên kit A giữ B1 2 s (hoặc `link bind`): cả hai bíp, kit A hiện `LINK 100%`,
   kit B hiện `RX 250/s`.
3. Không có joystick: `python tools/rc_pc_sticks.py --port COMx --arm` trên kit A, quét cần gạt chậm; thanh R/P/Y/T trên
   OLED kit B chạy theo.
4. Có joystick: `link cal` (cần gạt ở giữa, ga thấp nhất), trong 10 s gạt mọi cần ra hết các phía.
5. Arm: ga thấp nhất, giữ B3 1 s → kit B `ARMED`. Tắt kit A → trong 200 ms kit B `FAILSAFE`, bíp trầm. Bật lại kit A với
   công tắc arm vẫn bật: kit B vẫn `SAFE` cho tới khi arm lại.
6. Ghi `link` của cả hai sau 1 phút: tỉ lệ ACK, gói/s. Qua P1 khi 250 gói/s và mất < 1 % trong nhà.

**Phím của tay điều khiển:**
- B1 bấm: angle ↔ acro.
- B1 giữ 2 s: bind.
- B2: bật/tắt còi tìm drone.
- B3 bấm: disarm.
- B3 giữ 1 s: arm.

**Phím của bên thu:** B3 giữ 1 s: bind.

## 7. Nguồn

- Silverware cho H8 mini bo xanh — STM32F030, XN297L, `LOOPTIME 1000`, PWM 16 kHz, giao thức Bayang:
  [H8mini_blue_board](https://github.com/silver13/H8mini_blue_board),
  [config.h](https://raw.githubusercontent.com/silver13/H8mini_blue_board/master/Silverware/src/config.h),
  [h8mini-dual (vòng 1 ms)](https://github.com/silver13/h8mini-dual),
  [Brushless Silverware](https://github.com/vidzo/Brushless-Silverware).
- Bo brushed thương mại dùng giao thức Bayang: [BETAFPV Lite Brushed V2 (STM32F042 + MPU6050)](https://www.getfpv.com/betafpv-lite-brushed-v2-flight-controller.html).
- Betaflight bỏ STM32F1 (Naze32, CC3D, CJMCU) từ bản 3.3:
  [commit của Cleanflight](https://github.com/cleanflight/cleanflight/commit/9b4585769c7c83ea419e6c44a17402812cd1317d),
  [bản mirror của Betaflight](https://git.nekocwd.duckdns.org/Mirror/betaflight/commit/cec1b1a937f41e9c25f0cd08d7275add7a01c21e.diff).
- Crazyflie 2.1: [datasheet](https://www.bitcraze.io/documentation/hardware/crazyflie_2_1/crazyflie_2_1-datasheet.pdf),
  [bộ điều khiển xếp tầng](https://bitcraze.io/documentation/repository/crazyflie-firmware/master/functional-areas/sensor-to-control/controllers/),
  [STM32F405 + nRF51822](https://core-electronics.com.au/crazyflie-2-1-seeed-studio.html).
- Module Multiprotocol (nRF24, CC2500, A7105, CYRF6936; có Bayang; công cụ dump XN297L):
  [wiki](https://github.com/pascallanger/DIY-Multiprotocol-TX-Module/wiki),
  [Advanced Topics](https://github.com/glisandro/DIY-Multiprotocol-TX-Module/blob/master/docs/Advanced_Topics.md).
- XN297 so với nRF24: [diễn đàn Deviation](https://www.deviationtx.com/forum/6-general-discussions/5226-new-source-for-nrf24l01-module).

Không mở được trong lượt này: tài liệu chính thức về tần số vòng của Crazyflie (diễn đàn ghi 500 Hz và 1 kHz ở hai
chỗ khác nhau), chi tiết thanh ghi của Bayang trong mã Multiprotocol.
