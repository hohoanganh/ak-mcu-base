# Skill `ak-mcu-firmware` cho AI agent

Repo có sẵn một **skill** theo chuẩn [Agent Skills](https://agentskills.io): một tệp `SKILL.md` gom luật của kernel AK,
bản đồ bộ nhớ, cách thêm task, lệnh chẩn đoán và checklist review. AI agent (Claude Code, Codex và các công cụ theo cùng
chuẩn) tự nạp nó khi việc đang làm liên quan tới ak-mcu-base.

| Tệp | Dùng cho |
|---|---|
| [`.claude/skills/ak-mcu-firmware/SKILL.md`](../.claude/skills/ak-mcu-firmware/SKILL.md) | Claude Code |
| [`.agents/skills/ak-mcu-firmware/SKILL.md`](../.agents/skills/ak-mcu-firmware/SKILL.md) | Codex và công cụ theo chuẩn chung (bản giống hệt, CI so hai bản) |
| [`AGENTS.md`](../AGENTS.md) | Mọi agent đọc khi mở repo: tóm tắt luật và trỏ về [`CLAUDE.md`](../CLAUDE.md) |

## Skill đem lại gì

Agent không mang sẵn hiểu biết về dự án: mỗi phiên bắt đầu từ con số không, chỉ biết những gì nó tự đọc được trong lúc
làm. Skill là phần kinh nghiệm đã viết sẵn, agent đọc trước khi động tay.

**Không có skill**, agent làm việc như một lập trình viên giỏi nhưng mới vào dự án:

- **Viết theo thói quen chung của STM32.** Gặp chỗ cần chờ là dễ viết `HAL_Delay(100)` hay `while (!flag)` ngay trong
  handler. Code vẫn biên dịch, vẫn chạy, nhưng phá luật của kernel AK: board treo hoặc mất message khi tải nặng, không
  báo lỗi gì.
- **Phải đi dò.** Vector của app ở `0x08003100`, phải nạp `app.img` chứ không phải `.bin`, thêm task phải sửa hai chỗ
  cùng vị trí: muốn biết thì phải đọc nhiều tệp, và vẫn có thể sót.
- **Không biết các bẫy đã gặp:** `StructInit` của thư viện SPL, dây SWD làm sai lý do reset, đầu dò ST-Link mang firmware
  cũ, nạp đè bootloader mà quên reset.
- **Không biết thói quen an toàn:** dump flash trước khi nạp đè, hỏi trước khi đụng board thật, đọc `crash` trước khi đoán.

**Có skill**, agent nạp những điều trên đúng lúc việc liên quan tới AK:

- **Viết đúng kiểu AK từ đầu:** cần chờ thì `timer_set()`; ISR chỉ đẩy ring buffer hoặc post message; task chỉ nói
  chuyện bằng message.
- **Ít phải dò:** trả lời nhanh và đúng về địa chỉ, lệnh build, cách thêm task, lệnh shell, Modbus.
- **Review có checklist cụ thể:** 12 điểm rút từ lỗi thật, thay vì nhận xét chung chung.
- **Biết khi nào phải dừng lại hỏi:** trước khi nạp, xoá flash hay reset board; và không báo "đã build" khi chưa build.

## Ví dụ

Một người clone ak-mcu-base rồi nhờ agent: *"thêm task đọc cảm biến mỗi giây"*.

| | Không có skill | Có skill |
|---|---|---|
| Cách chờ | vòng `while` có delay, hoặc `TIMER_PERIODIC` cho việc có thể chạy quá chu kỳ | timer một lần, đặt lại cuối handler |
| Đăng ký task | dễ quên dòng trong `app_task_table`, hoặc đặt lệch vị trí | thêm đúng enum và dòng bảng cùng vị trí |
| Kết quả | board báo `FATAL TK 0x08` lúc khởi động, hoặc treo khi pool message cạn | chạy `make test`, rồi mới đề nghị nạp |

## Giới hạn

- Skill là hướng dẫn, không phải bảo đảm: agent vẫn có thể làm sai. Vẫn phải chạy `make test` và review.
- Skill phải đúng với mã. Đổi API, bản đồ bộ nhớ hay lệnh shell thì sửa skill trong cùng commit, cả hai bản.
- Trong chính repo này, [`CLAUDE.md`](../CLAUDE.md) đã đầy đủ hơn. Skill có ích nhất ở **dự án xuất bằng
  `tools/new_project.py`** (nó được chép theo) và với **agent không đọc `CLAUDE.md`**.

## Dùng ở dự án khác

Chép thư mục `ak-mcu-firmware` vào `~/.claude/skills/` (Claude Code) hoặc `~/.agents/skills/` (Codex). Dự án tạo bằng
`tools/new_project.py` đã có sẵn cả hai bản và `AGENTS.md`.
