# Ý tưởng demo tiếp theo cho AK Base Kit

Lập ngày 06/10/2026 từ một lượt tra cứu các dự án có phần cứng tương tự (OLED đơn sắc 128×64 và vài nút bấm:
Arduboy, TinyJoypad, các demo SSD1306/u8g2). Xếp theo "độ ấn tượng trên công sức" cho kit này.

Lưu ý về nguồn: danh sách dựa trên tóm tắt kết quả tìm kiếm, **chưa mở và chưa build thử** các repo được dẫn.
Chỗ nào ghi "chưa kiểm chứng" là không có nguồn đứng sau.

Ràng buộc của kit: STM32L151CB (Cortex-M3 32 MHz, không FPU, 32K RAM), 3 nút, còi thụ động, RTC, cảm biến SHT45,
flash SPI, RS485. Gửi trọn một khung ra OLED mất khoảng 40 ms, nên hiệu ứng nào đổi cả màn hình sẽ chạy 12–20 khung/giây;
hiệu ứng chỉ đổi một vùng thì rẻ hơn nhiều vì base chỉ gửi những trang đã thay đổi.

Cỡ việc: S = vài giờ, M = một hai ngày, L = nhiều ngày.

## Đã làm

| Việc | Ghi chú |
|---|---|
| GIF cho từng demo, dựng từ chính mã vẽ của firmware | `tests/test_demo.c` quay khung, `tools/demo_gif.py` dựng GIF |
| Chế độ tự chơi ("attract mode") | Lệnh shell `ui auto`; bấm nút là lấy lại quyền điều khiển |
| Khối 3D quay (mục 1) | Lập phương, bát diện, kim tự tháp; khung dây hoặc tô dither. 34–37 ms mỗi khung trên kit. Chưa làm lưới nhiều mặt kiểu ấm trà |
| Dino runner (mục 2) | B1 nhảy, B2 cúi, có tự chơi |
| Video từ flash SPI (mục 5) | Trình phát + tool `ak_video.py`; đã phát clip thử trên kit. Chưa thử chính đoạn Bad Apple: repo không kèm video |
| Trạm thời tiết (mục 6) | SHT45, đồ thị 96 điểm, `th csv`. Chưa ghi log vào flash: số liệu nằm trong RAM |
| Mê cung raycaster (mục 8) | Tường tô 3 mức, cửa ra, tự đi. Chưa có vật thể hay kẻ địch |
| Màn hình chờ (mục 4) | Game of Life trên lưới 64×32 (ô 2×2 điểm ảnh, không phải trọn 128×64), trường sao, plasma |
| Tetris, Breakout, Invaders (mục 7) | Ba nút: trái, phải, hành động; Tetris thả nhanh bằng cách giữ cả B1 và B2. Chưa lưu điểm cao vào flash |
| Truyền màn hình về máy tính (mục 9) | `ui dump` / `ui stream` + `tools/ak_screen.py` (chụp, quay GIF, cửa sổ trực tiếp có phím bấm) |
| Pong qua RS485 (mục 10) | Đã thử với máy tính đóng vai kit thứ hai (`tools/pong_peer.py`). Chưa thử hai kit thật |
| Máy hiện sóng mini (mục 11) | Màn hình Scope: số liệu từ lệnh `plot` hoặc thanh ghi Modbus 16; `tools/ak_plot.py` |
| Chạy trên trình duyệt (mục 12) | <https://hohoanganh.github.io/ak-mcu-base/play/>: cả firmware biên dịch sang WebAssembly, `port/web/` |
| Máy hát RTTTL (mục 3) | 6 bài nằm trong flash của chip, một kênh. Chưa lưu bài trong flash SPI, chưa thử hợp âm rải |

Tham khảo cách làm: trình giả lập [Ardens](https://github.com/retrom-project/Ardens) và
[ProjectABE](https://github.com/felipemanga/ProjectABE) của Arduboy đều có chức năng chụp ảnh và quay GIF.

## Nên làm tiếp

| # | Ý tưởng | Cỡ | Vì sao đáng làm | Khoe phần nào của kit | Tham khảo |
|---|---|---|---|---|---|
| ~~1~~ | ~~Khối lập phương 3D quay~~ (đã làm); còn lại: lưới nhiều mặt kiểu ấm trà | S → M | Bản Arduboy dựng ấm trà 240 mặt hơn 30 khung/giây trên AVR 8 bit; Cortex-M3 dư sức | Màn hình, CPU | [arduboy3d](https://github.com/a1k0n/arduboy3d) · [U8G2 graphics demos](https://github.com/OkuboHeavyIndustries/U8G2_SSD1306_graphics_demos) |
| ~~2~~ | ~~Dino runner~~ (đã làm): một nút nhảy, một nút cúi | S | Ai nhìn GIF cũng nhận ra ngay | Màn hình, nút, còi | [t-rex-duino](https://github.com/AlexIII/t-rex-duino) |
| ~~3~~ | ~~Máy hát chiptune / RTTTL~~ (đã làm), nốt nhạc chạy trên màn hình, bài hát lưu trong flash SPI | S | Hợp tự nhiên với kernel dùng timer mềm; cần video có tiếng, GIF không thể hiện được | Còi, flash, nút | [PlayRtttl](https://github.com/ArminJo/PlayRtttl) |
| ~~4~~ | ~~Bộ màn hình chờ~~ (đã làm): Game of Life, trường sao, plasma | S mỗi cái | Life trọn 128×64 chỉ cần hai bộ đệm 1 KB | Màn hình, RTC (làm hạt giống ngẫu nhiên) | [cgol_i2c_oled](https://github.com/weightan/cgol_i2c_oled_arduino) · [SSD1306-GameOfLife](https://github.com/babonev/SSD1306-GameOfLife) |
| ~~5~~ | ~~Video Bad Apple~~ (đã có trình phát; clip tự nạp) phát từ flash SPI (nén RLE + heatshrink) | M | Đoạn phim "màn hình bé làm được gì" nổi tiếng nhất | Flash SPI, driver màn hình | [ESP32_BadApple](https://github.com/hackffm/ESP32_BadApple) · [pico-badapple](https://github.com/HaruYou27/pico-badapple) |
| ~~6~~ | ~~Trạm thời tiết~~ (đã làm, trừ log vào flash): nhiệt độ, độ ẩm chữ to, đồ thị 24 giờ, ghi log vào flash, xuất CSV qua shell | S – M | Không hào nhoáng nhưng làm kit trông như một sản phẩm | SHT45, RTC, flash, UART | [Ví dụ trạm thời tiết SSD1306](https://simple-circuit.com/weather-station-arduino-bme280-ssd1306/) |
| ~~7~~ | ~~Tetris, Breakout, Space Invaders~~ (đã làm) | S – M mỗi cái | Bộ TinyJoypad chạy được trên ATtiny85 với cùng màn hình | Nút, còi, flash (điểm cao) | [CH32V003-GameConsole](https://github.com/wagiminator/CH32V003-GameConsole) · [Tiny-invaders](https://github.com/Lorandil/Tiny-invaders-v4.2) |
| ~~8~~ | ~~Raycaster~~ (đã làm) kiểu Wolfenstein: trái, phải, tiến | M – L | Ấn tượng nhất trong các demo đơn lẻ | CPU, màn hình | [Raycaster-Arduino-SSD1306](https://github.com/kouzerumatsukite/Raycaster-Arduino-SSD1306) · [Arduboy3D](https://github.com/jhhoward/Arduboy3D) |
| ~~9~~ | ~~Truyền màn hình về máy tính~~ (đã làm) qua UART, máy tính gửi ngược lại lệnh bấm nút | S – M | Cách Flipper Zero được trình diễn trong mọi video; shell `ui` đã làm được một nửa | UART, shell | — |
| ~~10~~ | ~~Hai kit chơi Pong qua RS485~~ (đã làm, chưa thử hai kit thật) | M | Demo duy nhất dùng cổng RS485 cho việc vui | RS485, hai kit | [Mod nhiều người chơi qua cổng nối tiếp trên Arduboy](https://community.arduboy.com/t/network-of-the-damned-multiplayer-mod-for-catacombs-of-the-damned/8367) |
| ~~11~~ | ~~Máy hiện sóng mini / vẽ đồ thị~~ (đã làm) giá trị nhận qua UART hoặc thanh ghi Modbus | S – M | Dụng cụ dùng được thật trên bàn làm việc | UART, RS485 | [Arduino OLED oscilloscope](https://hackaday.io/project/178003-arduino-oled-oscilloscope) |
| ~~12~~ | ~~Chạy thử ngay trên trình duyệt~~ (đã làm) (build host sang WebAssembly) | L | Một đường link "bấm là chơi" là thứ làm README sống động nhất | Tính di động của firmware | [TinyJoypad SDL chơi online](https://joyrider3774.github.io/Tinyjoypad_SDL/) · [lv_web_emscripten](https://github.com/lvgl/lv_web_emscripten) |
| 13 | **Đồng hồ kim, pomodoro, máy đếm nhịp, báo thức** | S mỗi cái | Ít ấn tượng nhưng làm đầy bộ ảnh với chi phí thấp | RTC, còi | — |

Ghi chú cho từng mục:

- **Mục 1 và 8:** chuyển động phủ cả màn hình nên mỗi khung tốn trọn 40 ms. Giữ vật thể trong một cửa sổ nhỏ để ít trang
  phải gửi hơn.
- **Mục 3:** nhạc nhiều kênh trên một còi PWM phải dùng hợp âm rải nhanh; có nghe được trên còi của kit hay không thì
  chưa kiểm chứng.
- **Mục 5:** cả đoạn phim có vừa flash 1 MB hay không thì chưa kiểm chứng; nên tính 10–15 khung/giây hoặc cắt ngắn.
  Việc chỉ gửi trang đã đổi giúp nhiều vì phim có nhiều vùng đứng yên.
- **Mục 7:** cách gán ba nút (trái / phải / hành động, giữ lâu cho hành động thứ hai) là đề xuất của tài liệu này,
  chưa kiểm chứng; TinyJoypad gốc dùng cần điều khiển và một nút.
- **Mục 10:** không tìm thấy ví dụ Pong qua RS485 nào, nên riêng cấu hình này chưa kiểm chứng.

Không tìm được nguồn đủ chắc nên không đưa vào: Mandelbrot trên SSD1306, game nhịp điệu, hiệu ứng đường hầm, lửa, metaball.

## Cách các dự án tương tự trình bày

- GIF chính ở đầu README, sau đó là lưới GIF hoặc ảnh nhỏ cho từng demo ([bộ ảnh của u8g2](https://github.com/olikraus/u8g2/wiki/gallery)).
- Bản chơi được trên web (Ardens, TinyJoypad SDL).
- Ảnh do trình giả lập sinh ra thay vì chụp bằng máy ảnh, nên luôn khớp với mã hiện tại. Base đã làm theo cách này.
