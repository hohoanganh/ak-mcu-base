# Hướng tối ưu tiếp theo cho ak-mcu-base

Lập ngày 06/10/2026 từ một lượt tra cứu tài liệu công khai. Mỗi mục ghi lợi ích, chi phí, rủi ro và nguồn.
Số liệu nào **chưa đối chiếu được với trang nguồn** đều ghi rõ "chưa kiểm chứng": phải đo lại trên board trước
khi dựa vào.

> Cập nhật 15/10/2026: mục 1 (xác nhận và quay lui) và mục 3 (ký số ảnh) đã làm trong v1.3.9. Các hướng tiếp theo,
> dựa trên số đo của chính base: [huong-toi-uu-sau-v1.3.9.md](huong-toi-uu-sau-v1.3.9.md).

## Đã làm trong v1.2.0

| Việc | Ghi chú | Nguồn tham khảo |
|---|---|---|
| Nhật ký sự cố bền qua reset | Ghi sau khi khởi động lại, không ghi trong handler lỗi | [Memfault: HardFault debug](https://interrupt.memfault.com/blog/cortex-m-hardfault-debug) · [Reboot reason tracking](https://docs.memfault.com/docs/mcu/reboot-reason-tracking) |
| Giám sát từng task trên nền watchdog cứng | Vỗ watchdog từ task, không vỗ từ ngắt timer | [Memfault: Watchdog best practices](https://interrupt.memfault.com/blog/firmware-watchdog-best-practices) |
| Đo mức dùng stack bằng mẫu tô sẵn | Cortex-M3 không có thanh ghi giới hạn stack (MSPLIM) | [Memfault: MPU](https://interrupt.memfault.com/blog/fix-bugs-and-secure-firmware-with-the-mpu) |
| CI: test host + build ARM + giới hạn kích thước | | [Memfault: firmware size tools](https://interrupt.memfault.com/blog/best-firmware-size-tools) |
| Cờ cảnh báo `-Wshadow -Wundef -Wdouble-promotion` | Thử trên toàn bộ mã của base: không cảnh báo nào | [Memfault: compiler flags](https://interrupt.memfault.com/blog/best-and-worst-gcc-clang-compiler-flags) |

Về kích thước mã: base đã dùng `-Os`, `--gc-sections`, LTO và newlib-nano. Ví dụ của Memfault cho thấy chính bốn thứ
đó mang lại gần hết mức giảm (72 → 50 KiB), nên phần còn lại không đáng kể. `-fno-common` thử trên base không đổi
byte nào. Các cờ `-fno-unwind-tables`, `-fmerge-all-constants`, `-fno-jump-tables` không tìm được nguồn định lượng:
chưa kiểm chứng, phải đo trước khi dùng. Nguồn: [code size optimization](https://interrupt.memfault.com/blog/code-size-optimization-gcc-flags),
[LTO và mã khởi động](https://m0agx.eu/fixing-cortex-m-startup-code-for-link-time-optimization.html).

## Nên làm tiếp, theo thứ tự giá trị trên công sức

### 1. Xác nhận ảnh mới và quay lui (confirm + rollback)

- **Là gì:** ảnh mới chạy ở trạng thái "thử"; app tự kiểm (Modbus lên, kernel chạy đủ N giây) rồi mới đánh dấu "ổn".
  Reset mà chưa đánh dấu thì bootloader quay về ảnh cũ. Thêm kiểm số phiên bản để chống hạ cấp.
- **Lợi ích:** đóng rủi ro lớn nhất của OTA hiện trường: ảnh đúng CRC nhưng chạy sai làm mất luôn đường cập nhật.
- **Chi phí:** `boot_ctrl` hai bản ghi và bộ đếm 3 lần thử đã có sẵn phần lớn trạng thái cần dùng.
- **Rủi ro:** base cài kiểu chép đè lên APP, nên muốn quay lui phải giữ ảnh cũ trên flash SPI (thêm một vùng staging
  thứ hai, hoặc sao lưu trước khi chép đè).
- **Nguồn:** [MCUboot design](https://docs.mcuboot.com/design.html).

### 2. Tiết kiệm điện: Stop mode với RTC đánh thức

- **Là gì:** timer của kernel đã dùng mốc tuyệt đối, nên "idle không tick" làm được thẳng: tính timer gần nhất, dừng
  SysTick, đặt RTC wakeup, vào Stop, khôi phục clock, cộng bù thời gian.
- **Lưu ý từ ST:** dừng SysTick trước khi ngủ; chân không dùng đặt analog; che ngắt quanh lúc vào Stop để kịp cấu
  hình lại clock; đầu dò debug đang cắm làm tăng dòng khoảng 100 µA nên phải ngắt nguồn trước khi đo.
- **Số liệu:** Stop + RTC khoảng 0,8 – 1,5 µA theo AN3193. **Chưa kiểm chứng** (không tải được PDF).
- **Rủi ro:** sau Stop phải bật lại HSE + PLL. Thiết bị RS485 slave đánh thức theo cạnh RX sẽ mất khung đầu.
- **Nguồn:** [ST: tips for low-power modes](https://community.st.com/t5/stm32-mcus/tips-for-using-stm32-low-power-modes/ta-p/621007) ·
  [AN3193](https://www.st.com/resource/en/application_note/an3193-stm32l1xx-ultralow-power-features-overview-stmicroelectronics.pdf).

### 3. Ký số ảnh

- **Là gì:** kiểm chữ ký ECDSA P-256 trong bootloader, khoá công khai nằm trong bootloader.
- **Kích thước:** thư viện p256-m khoảng 2,9 – 3,0 KB mã, 700 – 752 B stack khi verify, chưa gồm SHA-256.
  Thời gian verify trên Cortex-M3 32 MHz **chưa kiểm chứng**, ước khoảng 1 giây.
- **Rủi ro:** bootloader đang dùng 7,9K trên 12K, thêm P-256 và SHA-256 là rất sát. Nới bootloader lên 16K sẽ đổi
  địa chỉ link của app và làm lệch với thiết bị đã bán. Việc khó thật sự là quản lý khoá.
- **Nguồn:** [p256-m](https://github.com/mpg/p256-m) · [micro-ecc](https://github.com/kmackay/micro-ecc).

### 4. Cập nhật nén hoặc theo phần chênh (delta)

- **Là gì:** giải nén heatshrink (bộ giải mã khoảng 1 KB, RAM cửa sổ 256 B) hoặc vá delta bằng janpatch / detools.
- **Lợi ích:** ví dụ của Memfault: bản vá 1.252 B thay cho 7.908 B. Đáng giá khi đường truyền là RS485 baud thấp.
- **Rủi ro:** một bản delta chỉ khớp đúng một phiên bản gốc; phải kiểm CRC hoặc chữ ký của ảnh sau khi dựng lại.
  Nên làm sau mục 1 và mục 3.
- **Nguồn:** [Memfault: OTA delta updates](https://interrupt.memfault.com/blog/ota-delta-updates) ·
  [heatshrink](https://github.com/atomicobject/heatshrink).

### 5. Vùng chắn stack bằng MPU, và giới hạn stack lúc build

- **Là gì:** một vùng MPU cấm truy cập ở đáy stack, tràn stack sẽ ra lỗi MemManage ngay thay vì âm thầm đè heap.
  Kèm `-fstack-usage` và `-Wstack-usage=N` để biết hàm nào ăn stack lúc build.
- **Chi phí:** một vùng MPU, tối thiểu 32 B, kích thước và căn lề theo luỹ thừa của 2.
- **Rủi ro:** handler lỗi cũng cần stack, phải dành stack riêng cho nó.
- **Nguồn:** [Memfault: MPU](https://interrupt.memfault.com/blog/fix-bugs-and-secure-firmware-with-the-mpu).

### 6. Log nhị phân và test theo vết (ý tưởng của QP)

- **Là gì:** ghi vết dạng nhị phân gọn vào bộ đệm RAM, giải mã trên máy tính; test task bằng cách bơm message rồi
  đối chiếu vết.
- **Lợi ích:** giảm flash và thời gian UART của log dạng chuỗi; hợp với trình giả lập sẵn có.
- **Nguồn:** [QS software tracing](https://www.state-machine.com/qpc/srs-qp_qs.html) · [QTools](https://www.state-machine.com/qtools/qs.html).

## Việc riêng của base, không cần tra cứu

- Kênh OTA qua Modbus RS485 và lớp Modbus (nanoMODBUS) như base cũ.
- Port chip thứ hai để chứng minh lớp HAL đủ tổng quát.
- Tìm hiểu vì sao sau khi cấp nguồn lại app từng báo lý do reset là "software": giờ đã có nhật ký sự cố để lần ra.
