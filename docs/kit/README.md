# Ảnh và sơ đồ của AK Base Kit

| File | Nội dung | Nguồn |
|---|---|---|
| `ak-base-kit-v3.jpg` | Ảnh chụp kit bản 3 | [AK Foundation](https://github.com/the-ak-foundation/ak-base-kit-stm32l151/tree/main/hardware/images), giấy phép MIT; đã thu nhỏ |
| `board-view-top.png` | Sơ đồ mặt trên của bo, có tên các chân cắm | AK Foundation, cùng nguồn, giấy phép MIT |
| `pcb-3d-render.jpg` | Bản dựng 3D của bo mạch bản 3.0 | Xuất từ file thiết kế |
| `ak-base-kit-3i0-3d.html` | Mô hình 3D của bo 3.0, xoay được; nhấn vào linh kiện thì hiện thông tin (118 linh kiện, có ô tìm). Một file tự chứa 11,8 MB, không gọi ra mạng | Dựng từ file STEP của bo 3.0; thông tin linh kiện lấy từ BOM và Pick&Place. Mô tả của U1 đã sửa tay thành 128 Kbytes Flash (BOM ghi 64) |
| `ak-base-kit-3i0-render-top.jpg` | Ảnh dựng mặt trên: màn hình OLED và nút bấm | Dựng bằng Blender (Cycles) từ cùng mô hình |
| `ak-base-kit-3i0-render-bottom.jpg` | Ảnh dựng mặt dưới: vi điều khiển, pin, cổng nối | Cùng cách |
| `stm32l151-pinout.svg` | Sơ đồ chân STM32L151CBT6 trên kit | Sinh bởi `tools/pinout_svg.py`; đã đối chiếu với schematic AK MCU KIT 3.0 |
| `block-diagram.svg` | Sơ đồ khối của bo | Sinh bởi `tools/diagrams_svg.py` theo schematic AK MCU KIT 3.0 |
| `power-tree.svg` | Cây nguồn | Cùng script, theo trang Power của schematic |
| `connectors.svg` | Các cổng nối của bo, từng chân | Sinh bởi `tools/connectors_svg.py` theo schematic AK MCU KIT 3.0 |
