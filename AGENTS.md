# AGENTS.md

Hướng dẫn cho mọi AI agent làm việc trong repo này (Codex, Cursor, Copilot, Gemini, Claude Code…).
*Guidance for any AI coding agent. Docs are in Vietnamese; code and comments are in English.*

- **Hướng dẫn đầy đủ:** [CLAUDE.md](CLAUDE.md) — cấu trúc repo, lệnh build/test, luật kernel, phần cứng, phát hành. Tên
  file là của Claude Code nhưng nội dung dùng chung cho mọi agent; đọc nó trước khi sửa code.
- **Skill** (chuẩn Agent Skills, nạp khi việc liên quan): [`.agents/skills/ak-mcu-firmware`](.agents/skills/ak-mcu-firmware/SKILL.md)
  cho Codex và các công cụ theo chuẩn chung, bản giống hệt ở `.claude/skills/` cho Claude Code. Sửa thì sửa cả hai
  (CI so hai bản). Skill giúp gì: [docs/agent-skill.md](docs/agent-skill.md).

## Luật không được phá (kernel AK: Active Object, không RTOS, không preemptive)

1. ISR chỉ đẩy byte vào ring buffer hoặc post message. Mọi xử lý nằm trong task.
2. Handler chạy tới khi xong rồi return: không vòng chờ, không delay dài; cần chờ thì `timer_set()`.
3. Task chỉ nói chuyện bằng message, không gọi chéo hàm, không dùng chung biến ghi từ hai nơi.
4. Việc định kỳ có thể chạy quá chu kỳ thì dùng timer một lần, không dùng `TIMER_PERIODIC`.
5. Thêm task: enum trong `src/app/task_list.h` và dòng trong `app_task_table` phải cùng vị trí.

## Lệnh

```bash
make test                         # test trên máy tính (Linux/WSL). Thay đổi chưa qua lệnh này thì chưa xong
pio run -e boot -e app -e demo    # firmware STM32L151
```

Không có toolchain thì nói rõ là chưa build, chưa test. Thao tác lên board thật (nạp, xoá flash, reset) phải nói rõ và
chờ người dùng đồng ý. Không sửa `third_party/` và `legacy/` trừ khi được giao. Repo cá nhân: không đưa tên công ty, thông
tin liên hệ hay tên sản phẩm thương mại vào mã và tài liệu.
