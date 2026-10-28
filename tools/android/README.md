# AK Flash: nạp firmware và console từ điện thoại Android

App Android dùng lại giao thức `fw_proto` của `tools/ak_fw.py` để làm việc với board ak-mcu-base qua cáp **USB OTG** và
một bộ chuyển USB-UART cắm vào cổng console. Ra hiện trường không cần mang laptop hay ST-Link.

| Nút | Tương đương | Làm gì |
|---|---|---|
| Thông tin | `ak_fw.py info` | Tên board, đang chạy bootloader hay app, phiên bản, dung lượng vùng staging |
| Nạp .img | `ak_fw.py flash x.img` | Chọn file, tải lên, board kiểm CRC, cài, chờ app mới chạy |
| Xác nhận | `ak_fw.py confirm` | Kết thúc chạy thử của ảnh mới (không bấm thì app tự xác nhận sau 30 s) |
| Quay lui | `ak_fw.py rollback` | Cài lại ảnh trước lần cập nhật gần nhất |
| Bootloader / Chạy app | `ak_fw.py loader` / `run` | Chuyển giữa app và bootloader |
| Reset | `ak_fw.py reset` | Khởi động lại board |
| Ô lệnh dưới cùng | terminal | Gõ lệnh shell (`help`, `fw`, `crash`, `ver`…); log của board hiện ở khung giữa |

Bộ chuyển chạy được: CH340/CH341/CH9102, CP210x, FTDI, PL2303 và cổng COM ảo (CDC) của ST-LINK/V2-1 trên Nucleo (thư
viện [usb-serial-for-android](https://github.com/mik3y/usb-serial-for-android)). Tốc độ mặc định 115200.

**ST-Link vẫn cần cho lần nạp bootloader đầu tiên.** Từ đó về sau app và bản cập nhật đi qua UART. App không nạp qua
SWD.

## Cấu trúc

```
tools/android/
  proto/   giao thức fw_proto bằng Kotlin thuần (JVM): FwProto (khung, CRC), FrameParser, AkFw (client)
           test: các ca của tests/test_ak_fw.py, và SimOtaTest nạp đầu-cuối trên ak_sim
  app/     app Android (ak.mcu.fw): MainActivity, UsbLink (cổng USB serial)
```

`proto` không phụ thuộc Android, nên test được trên máy tính với `ak_sim`, giống `tests/test_sim_ota.py`. Sửa giao thức
ở `services/fw/` hoặc `tools/ak_fw.py` thì sửa cả `proto/` (bảng lỗi `ERRORS` theo `services/fw/fw_types.h`).

## Build

Cần JDK 17+. App cần thêm Android SDK (Android Studio, hoặc đặt `ANDROID_HOME`). Không có SDK thì Gradle chỉ build
`proto`.

```bash
cd tools/android

# test giao thức, kể cả nạp trên giả lập (bỏ AK_SIM thì bỏ qua ca đó)
cmake -S ../.. -B ../../build/host && cmake --build ../../build/host --target ak_sim
AK_SIM=../../build/host/ak_sim ./gradlew :proto:test

# APK
./gradlew :app:assembleDebug        # app/build/outputs/apk/debug/app-debug.apk
./gradlew :app:installDebug         # cài lên điện thoại đang cắm qua adb
```

Trên Windows dùng `gradlew.bat`, đường dẫn `AK_SIM` trỏ tới `ak_sim.exe` nếu có. Có thể mở thẳng thư mục
`tools/android` bằng Android Studio.

CI (job `android` trong `.github/workflows/ak-mcu-base.yml`) chạy test với `ak_sim` và build APK; tải APK ở mục
artifact `ak-flash-apk` của lần chạy.

## Dùng

1. Nối bộ USB-UART vào cổng console của kit (TX↔RX, GND chung), cắm vào điện thoại qua cáp OTG.
2. Android hỏi mở AK Flash: đồng ý và cấp quyền USB. App tự kết nối và hỏi `INFO`.
3. **Nạp .img** → chọn `app.img` (chép vào điện thoại trước, ví dụ tải artifact CI) → OK. Nạp `app.img`, **không nạp
   `.bin` trần**: bootloader chỉ nhận ảnh có header và CRC đúng.
4. Kiểm tra app mới chạy đúng rồi bấm **Xác nhận**. Không xác nhận sau 3 lần khởi động thì bootloader tự cài lại ảnh cũ.

## Trạng thái

- `proto`: 12 test qua, gồm nạp v1 qua bootloader, OTA v2 qua app, từ chối ảnh sai board, lệnh `ver` qua console,
  xác nhận, quay lui, bootloader rồi chạy app, tất cả trên `ak_sim`.
- `app`: khung đầu tiên, **chưa chạy trên điện thoại và kit**. Việc nên làm tiếp: lưu tốc độ baud, chọn khi có nhiều
  bộ chuyển, đọc header ảnh (phiên bản, tên board) trước khi nạp, nạp qua Modbus RTU/RS485 như `ak_mb.py`.
