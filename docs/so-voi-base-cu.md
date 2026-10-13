# ak-mcu-base so với base cũ (`legacy/sources/`): hơn gì, bằng gì, còn thiếu gì

Cập nhật: 06/10/2026 · `ak-mcu-base` v1.3.0 (bootloader 1.2.0) · so với `ak-base-kit-pio` v1.3.0 (bootloader 0.0.3)
· Board: AK Base Kit, STM32L151CBT6 (128K flash, 16K RAM)

Bản HTML (có bản đồ flash vẽ đúng tỉ lệ, in được): <https://hohoanganh.github.io/ak-mcu-base/ak-mcu-base-so-voi-base-cu.html> · nguồn: `docs/ak-mcu-base-so-voi-base-cu.html`.

Tài liệu này trả lời một câu hỏi: **khi nào nên bắt đầu dự án từ `ak-mcu-base` thay vì base cũ.**
Mỗi dòng "hơn" đều ghi rõ đã kiểm bằng gì. Chỗ nào bản mới chưa bằng bản cũ cũng ghi thẳng ở mục 4.

---

## 1. Tóm tắt

| | Base cũ (`legacy/sources/`) | `ak-mcu-base` |
|---|---|---|
| Mức hoàn thiện | Đầy đủ driver, Modbus, OTA qua RS485; đã dùng cho sản phẩm | Kernel, bootloader, OTA qua UART và RS485, Modbus RTU; ít driver |
| Phụ thuộc chip | Kernel, boot, app gọi thẳng SPL / lớp Arduino | Mọi thứ trên HAL không include header của chip |
| Kiểm thử trên máy tính | Chỉ lớp Modbus (`legacy/tests_host/modbus`) | Kernel, định dạng ảnh, bootloader, OTA đầu-cuối trên giả lập |
| Kiểm ảnh trước khi chạy | Dựa vào lệnh trong BSF + bảng vector hợp lý | CRC32 header + CRC32 toàn ảnh + tên board + địa chỉ nạp + bảng vector, **mỗi lần boot** |
| Mất điện giữa lúc cài | Dựa vào BSF (xoá rồi mới ghi) | Trang header xoá đầu, chép cuối; trạng thái boot có hai bản ghi |

**Khuyến nghị:** dự án mới dùng `ak-mcu-base`. Base cũ (v1.3.0) dành cho dự án đang chạy trên nó, hoặc khi cần ngay
driver mà base mới chưa có (EEPROM ngoài, nRF24, lớp Arduino).
Dự án mới mà phần OTA an toàn và khả năng chuyển chip quan trọng hơn bộ driver có sẵn → `ak-mcu-base`,
chấp nhận tự viết lại driver cần dùng.

---

## 2. Những điểm tối ưu hơn

### 2.1 Bootloader và cập nhật firmware

| Hạng mục | Base cũ | `ak-mcu-base` | Đã kiểm bằng |
|---|---|---|---|
| Toàn vẹn ảnh | Tổng 16 bit của các word, chỉ tính **sau khi chép** | CRC32 toàn ảnh, tính **mỗi lần boot** và trước khi cài | Unit test; trên board |
| Nạp nhầm ảnh | Chỉ có magic number trong header | Header mang tên board và địa chỉ nạp; ảnh của board khác bị từ chối ngay khi nhận xong | Unit test |
| Cài hỏng lặp lại | Checksum sai thì reset và thử lại mãi | Tối đa 3 lần, sau đó về chế độ loader chờ nạp | Unit test |
| Mất điện lúc cài | Xoá hết APP rồi chép; lần boot sau dựa vào lệnh còn trong BSF | Trang header của APP xoá đầu tiên, chép cuối cùng: ảnh chép dở luôn "không hợp lệ" | 3.252 điểm cắt điện trên giả lập, chạy cho cả hai kiểu staging |
| Trạng thái boot ↔ app | BSF 4K trong flash, xoá rồi mới ghi (xem `legacy/docs/known-bugs.md` #3) | 2 × 32 B trong EEPROM, hai bản ghi luân phiên có số thứ tự | Cắt ở từng byte của lần ghi (65 điểm); trên board: OTA liên tiếp |
| Giao thức nạp | Boot có giao thức UART riêng, app đi đường khác | Một giao thức (`fw_proto`) cho cả boot lẫn app, dùng chung UART với shell | Trên board |

Bản mới còn có thêm: tự cài lại từ staging khi APP hỏng, và chế độ `AK_STAGING=internal` (APP 58K + staging 58K
trong flash trong) cho board không gắn flash SPI — cả hai mới qua unit test.

Hai thứ **không** phải điểm mới, để khỏi hiểu lầm: ghi flash theo half-page và watchdog trong bootloader
thì base cũ đã có sẵn.

### 2.2 Kernel AK

API giữ nguyên (`task_post_*`, `timer_set`, `fsm`/`tsm`, bảng task). Các thay đổi đều có unit test:

| Hạng mục | Base cũ | `ak-mcu-base` |
|---|---|---|
| Timer chu kỳ khi một handler chạy lâu | Nạp lại `counter = period`, bỏ phần trễ → trôi dần | Mốc hết hạn tuyệt đối, giữ đúng pha |
| `timer_set()` gọi sau handler chạy N ms | Bị trừ luôn N ms đã dồn → nổ sớm | Tính từ thời điểm gọi |
| Tải của ngắt tick 1 ms | Mỗi ms cấp một message và duyệt cả danh sách timer | Chỉ post khi timer gần nhất đến hạn |
| `src_task_id` của message post từ polling task | Mang id của task chạy gần nhất | `AK_TASK_IDLE_ID` |
| Vòng lặp chính lúc rảnh | Quay liên tục | `WFI`; kiểm hàng đợi và ngủ trong cùng một critical section |
| Bảng task sai (`pri` = 0, sai thứ tự) | `pri` = 0 ghi ra ngoài mảng | `FATAL` ngay lúc khởi động |

Trên board, bộ đếm pool `pure` của app mẫu cao nhất chỉ 2 message sau nhiều phút chạy, khớp với việc tick
không còn post mỗi ms.

### 2.3 Kích thước và build

| | Base cũ v1.3.0 | `ak-mcu-base` v1.1.0 |
|---|---|---|
| Bootloader | 6.820 B / phân vùng 8K | 8.056 B / phân vùng 12K |
| Vùng trạng thái boot | BSF 4K trong flash | Không tốn flash (EEPROM) |
| Flash còn cho app | 116K tại `0x08003000` | 116K tại `0x08003000` (gồm 256 B header) |
| LTO | Không | Bật mặc định, boot và app nhỏ hơn khoảng 14% |
| Cảnh báo | — | `-Wall -Wextra -Werror` cho mã của base |

Bootloader mới **lớn hơn** 1,2K vì thêm CRC32, kiểm header, hai bản ghi trạng thái và chế độ cứu ảnh.
Tổng flash dành cho boot + trạng thái vẫn là 12K ở cả hai bên, nên app không mất byte nào.

### 2.4 Tốc độ và công cụ

- OTA qua UART 115200 bằng `tools/ak_fw.py`: ảnh 10,5K mất **1,6 s** kể cả verify trên board (trước khi sửa tool là 5,4 s).
  Không so trực tiếp được với OTA qua RS485 9600 của base cũ (khoảng 78 s/lượt) vì khác đường truyền và khác cỡ ảnh.
- Từ lúc reset tới lúc app chạy: khoảng 31 ms với ảnh 11K, đã gồm CRC toàn ảnh.
- `make test` chạy toàn bộ unit test và OTA đầu-cuối trên giả lập `ak_sim`, không cần board.
- `tools/mkimage.py patch` điền CRC vào cả `.img` lẫn `.elf`, nên nạp `.elf` qua SWD cũng ra ảnh hợp lệ.

### 2.5 Chuyển sang chip khác

Kernel, bootloader, dịch vụ OTA và app không include header của chip. Thêm chip mới là viết `port/<chip>/`
theo [porting.md](porting.md). Hiện có hai port: `stm32l151` và `host` (máy tính). Chưa có port chip thứ hai
để chứng minh lớp HAL đủ tổng quát.

---

### 2.6 Thêm ở v1.2.0 và v1.3.0

Nhật ký sự cố trong EEPROM, giám sát từng task, đo mức dùng stack, script xuất dự án mới, build PlatformIO và CI.
Modbus RTU trên RS485 (slave, master, OTA qua khối thanh ghi `0xF000`): [modbus.md](modbus.md). OTA ảnh 20K qua RS485 9600
mất 26 s, đo trên kit. Demo trên kit (đồng hồ số, Snake, Flappy): [demo-kit.md](demo-kit.md).
Chi tiết: [tien-ich.md](tien-ich.md). Các hướng tối ưu còn lại, kèm nguồn: [huong-toi-uu-tiep.md](huong-toi-uu-tiep.md).

## 3. Ngang nhau

- Console TX không chặn (ring + ngắt TXE): base cũ đã có, bản mới thêm ở v1.1.0.
- Ghi flash trong theo half-page từ RAM.
- Watchdog trong bootloader (cũ 32 s, mới 10 s).
- Bản đồ bộ nhớ cho app: 116K tại `0x08003000`, staging trên flash SPI tại `0x80000`.

---

## 4. Còn thiếu so với base cũ

| Thiếu | Ghi chú |
|---|---|
| Driver: nút, còi, OLED, nRF24, EEPROM ngoài, lớp Arduino | Cố ý không mang sang; viết lại theo nhu cầu từng sản phẩm |
| Ký số ảnh | Chỉ có CRC: chống hỏng dữ liệu, không chống giả mạo |
| Dự án con | Chưa dự án nào chuyển sang; các dự án hiện có vẫn theo base cũ |

**App và bootloader phải cùng đời:** bootloader 1.0.0 chỉ đọc bản ghi trạng thái thứ nhất nên bỏ sót lệnh
cài của app từ 1.1.0. Nạp bootloader trước qua SWD, rồi mới nạp hoặc OTA app.

---

## 5. Đã kiểm gì trên board (06/10/2026)

- Boot → app, shell (`help`, `ver`, `info`, `verify`, `stat`), OTA nhiều lượt qua bootloader 1.1.0.
- Chế độ loader 16 s không reset; dừng lõi 13 s bằng debugger thì watchdog của bootloader reset và quay lại đúng chế độ loader.
- Dồn 40 dòng lệnh khi một lệnh chậm đang chạy: không FATAL, dòng thừa bị bỏ kèm thông báo.

Chưa kiểm trên board: rút điện giữa lúc bootloader đang cài, OTA ảnh gần 116K, timeout của flash SPI,
độ chính xác timer bằng máy hiện sóng.
