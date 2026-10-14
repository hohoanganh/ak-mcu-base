/*
 * inside.js - the page "Bên trong firmware" (inside.html): how the source runs,
 * shown on the firmware of this page.
 *
 *   1  the four things the main loop does (kernel/src/task.c), with the real code
 *   2  every message the kernel hands to a task, read from the running firmware
 *      (kit.trace(), web_trace() in web_main.c); the firmware can be paused and
 *      run one message at a time (kit.pause(), kit.step())
 *   3  the order of things from power-on to the main loop
 *   4  an update with backup, trial and rollback: a simulation in the order the
 *      bootloader works (boot/boot_core.c), "pull the plug" at any step
 *   5  which folder does what
 */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var each = function (list, fn) { Array.prototype.forEach.call(list, fn); };
  var press = function (box, hit) { each(box.children, function (b) { b.setAttribute("aria-pressed", b === hit ? "true" : "false"); }); };
  var GH = "https://github.com/hohoanganh/ak-mcu-base/blob/main/";
  var link = function (path) { return 'Trong mã nguồn: <a href="' + GH + path + '">' + path + "</a>"; };

  /* ------------------------------------------------------------------ 1: the main loop */
  var STAGES = [
    { t: "Việc 1: có message nào không?",
      p: "Mỗi task có một hộp thư. Vòng lặp nhìn tất cả hộp thư và chọn hộp có ưu tiên cao nhất đang có message. Lấy message đứng đầu hộp đó.",
      c: "while ((t_task_new = LOG2LKUP(task_ready)) > t_task_current) {\n    tcb_t* t_tcb = &task_pri_queue[t_task_new - 1];   <i>/* hộp thư ưu tiên cao nhất đang có message */</i>\n    ak_msg_t* t_msg = t_tcb->qhead;                   <i>/* message đứng đầu hộp */</i>\n    t_tcb->qhead = t_msg->next;\n    ...",
      s: "src/kernel/src/task.c" },
    { t: "Việc 2: giao cho task, chờ nó làm xong",
      p: "Message được đưa cho đúng task. Task làm trọn việc đó rồi mới trả quyền lại; không ai chen ngang được. Vì thế mỗi việc phải ngắn: muốn chờ thì đặt hẹn giờ rồi về, không đứng đợi.",
      c: "    task_table[t_msg->des_task_id].task(t_msg);       <i>/* gọi task nhận message, chờ nó xong */</i>\n    ...\n    msg_free(t_msg);                                  <i>/* xong việc: bỏ tờ phiếu */</i>\n}",
      s: "src/kernel/src/task.c" },
    { t: "Việc 3: đi tuần một vòng",
      p: "Có những thứ không tự báo: nút bấm, chữ gõ vào dòng lệnh, đường RS485. Vòng lặp ghé hỏi từng thứ. Thấy gì mới thì không xử lý tại chỗ, chỉ viết một message bỏ vào hộp thư của task lo việc đó.",
      c: "while (__task_polling_table->id < AK_TASK_POLLING_EOT_ID) {\n    if (__task_polling_table->ability == AK_ENABLE) {\n        __task_polling_table->task_polling();        <i>/* ghé một chỗ: nút, dòng lệnh, RS485 */</i>\n    }\n    __task_polling_table++;\n}",
      s: "src/kernel/src/task.c" },
    { t: "Việc 4: hết việc thì ngủ",
      p: "Không còn message nào thì chip ngủ để đỡ tốn điện. Một ngắt (đồng hồ, có chữ tới) đánh thức nó, và vòng lặp quay lại việc 1. Kit đứng yên ngủ gần hết thời gian: mỗi 50 ms chỉ thức một lúc để vẽ màn hình.",
      c: "for (;;) {\n    if (task_run_once() == 0) {        <i>/* việc 1, 2, 3: không có message nào */</i>\n        ENTRY_CRITICAL();\n        if (task_ready == 0) {\n            ak_port_idle();            <i>/* ngủ, chờ ngắt */</i>\n        }\n        EXIT_CRITICAL();\n    }\n}",
      s: "src/kernel/src/task.c" }
  ];
  var stages = $("loop").children, liveTimers = [];
  function showStage(k) {
    var s = STAGES[k];
    press($("loop"), stages[k]);
    $("stT").textContent = s.t;
    $("stP").textContent = s.p;
    $("stC").innerHTML = s.c.replace(/&/g, "&amp;").replace(/<(?!\/?i>)/g, "&lt;");
    $("stS").innerHTML = link(s.s) + " · chú thích tiếng Việt là thêm vào để giải thích";
  }
  each(stages, function (b, k) { b.onclick = function () { showStage(k); }; });
  showStage(0);
  /* light the stages a message passed: [index, delay ms] */
  function lightStages(list) {
    liveTimers.forEach(clearTimeout);
    liveTimers = [];
    each(stages, function (s) { s.classList.remove("live"); });
    list.forEach(function (k, n) {
      liveTimers.push(setTimeout(function () { stages[k].classList.add("live"); }, n * 220));
      liveTimers.push(setTimeout(function () { stages[k].classList.remove("live"); }, n * 220 + 700));
    });
  }

  /* ------------------------------------------------------------------ 3: from power-on */
  var BOOT = [
    ["Cấp điện: chip chạy bootloader trước", "Bootloader là một chương trình nhỏ 12 KB nằm ở đầu bộ nhớ. Nó luôn chạy đầu tiên, trước mọi thứ khác.", "src/boot/boot_main.c"],
    ["Bootloader đọc tờ lệnh và kiểm bản firmware", "Có lệnh cài bản mới không? Bản đang nằm trong chip có nguyên vẹn không (tính lại tổng kiểm)? Tuỳ câu trả lời mà nó cài, quay lui, chờ nạp, hay cho chạy.", "src/boot/boot_core.c"],
    ["Nhảy sang firmware chính", "Bootloader khởi động lại chip rồi nhảy thẳng sang firmware chính ngay từ đầu, khi chưa bật bộ phận nào. Firmware luôn bắt đầu từ một con chip sạch.", "src/port/stm32l151/startup.c"],
    ["Firmware tự giới thiệu, đọc lại chuyện lần trước", "In ra phiên bản và tên kit. Nếu lần chạy trước kết thúc bằng một sự cố (treo, lỗi), nó chép thông tin đó vào sổ sự cố trước khi làm việc khác.", "src/app/main.c"],
    ["Tạo các task và danh sách đi tuần", "Từ một bảng: mỗi dòng là một task, mức ưu tiên và hàm xử lý. Bảng thứ hai liệt kê những chỗ phải đi tuần. Thêm một task là thêm một dòng.", "src/app/task_list.c"],
    ["Gửi những message đầu tiên", "Mỗi task nhận một message INIT để tự chuẩn bị: người gác đặt nhịp tim 1 giây, dòng lệnh in dấu nhắc, giao diện mở màn hình và đặt hẹn giờ vẽ khung đầu tiên.", "src/app/main.c"],
    ["Vào vòng lặp chính, không bao giờ thoát", "Từ đây chỉ còn bốn việc của mục 1, quay mãi. Mọi thứ kit làm sau đó đều bắt đầu từ một message.", "src/kernel/src/task.c"]
  ];
  BOOT.forEach(function (b, k) {
    var li = document.createElement("li"), btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = b[0];
    btn.setAttribute("aria-pressed", "false");
    btn.onclick = function () {
      each($("steps").children, function (x, j) { x.firstChild.setAttribute("aria-pressed", j === k ? "true" : "false"); });
      $("bT").textContent = b[0];
      $("bP").textContent = b[1];
      $("bS").innerHTML = link(b[2]);
    };
    li.appendChild(btn);
    $("steps").appendChild(li);
  });
  $("steps").children[0].firstChild.click();

  /* ------------------------------------------------------------------ 4: update */
  (function () {
    var OLD = "bản cũ", NEW = "bản mới", st;
    function tank(id, cls, pct, text, run) {
      var t = $(id);
      t.className = "tank " + cls + (run ? " run" : "");
      t.firstChild.style.width = pct + "%";
      t.lastChild.textContent = text;
    }
    function acts(list) {
      var box = $("acts");
      box.textContent = "";
      list.forEach(function (a) {
        var b = document.createElement("button");
        b.type = "button";
        b.className = "b " + (a[2] || "");
        b.textContent = a[0];
        b.onclick = a[1];
        box.appendChild(b);
      });
    }
    var TEXT = [
      "Kit đang chạy bản cũ. Máy tính có một bản mới.",
      "Bản mới đi vào ngăn <b>CHỜ</b>. Kit vẫn chạy bản cũ như thường.",
      "Nhận xong thì <b>kiểm</b>: đủ byte, đúng loại kit. Đạt thì để lại tờ lệnh “cài”, rồi khởi động lại.",
      "Bootloader thức dậy trước. Việc đầu tiên: <b>cất bản cũ</b> sang DỰ PHÒNG.",
      "Rồi mới chép bản mới vào chỗ chạy. <b>Tờ bìa chép sau cùng</b>: chưa có bìa thì chưa ai chạy nó.",
      "Bản mới chạy, nhưng mới là <b>chạy thử</b>. Nó có 3 lần khởi động để chứng minh."
    ];
    var again = ["Làm lại", function () { reset(); }];
    var zap = ["Rút điện", function () { pull(); }];
    function show(text) {
      var s = st.step;
      tank("tApp", s === 4 ? "half" : (st.app === NEW ? "new" : "old"), s === 4 ? 55 : 100,
           s === 4 ? "đang chép… chưa chạy được" : st.app + (st.trial ? " · chạy thử lần " + st.trial + " / 3" : " · đang chạy"), s !== 4);
      tank("tStg", st.stg === "" ? "" : (st.stg === "half" ? "half" : "new"), st.stg === "" ? 0 : (st.stg === "half" ? 60 : 100),
           st.stg === "" ? "trống" : (st.stg === "half" ? "đang nhận…" : NEW + (s >= 2 ? " · đã kiểm" : "")));
      tank("tBak", st.bak ? "old" : "", st.bak ? 100 : 0, st.bak ? OLD + " · cất giữ" : "trống");
      $("say").innerHTML = text;
      if (s === 5) {
        acts([["Chạy tốt 30 giây", function () { st.step = 6; st.trial = 0; show("<b>Chốt.</b> Bản mới ở lại. Bản cũ vẫn nằm trong DỰ PHÒNG, phòng khi cần."); }, "go"],
              ["Bản mới bị treo", function () { hang(false); }], zap]);
      } else if (s >= 6) {
        acts([again]);
      } else {
        acts([["Bước tiếp ▶", function () { next(); }, "go"], zap]);
      }
    }
    function reset() { st = { step: 0, app: OLD, stg: "", bak: false, trial: 0 }; show(TEXT[0]); }
    function next() {
      st.step++;
      if (st.step === 1) { st.stg = "half"; }
      if (st.step === 2) { st.stg = "full"; }
      if (st.step === 3) { st.bak = true; }
      if (st.step === 5) { st.app = NEW; st.trial = 1; }
      show(TEXT[st.step]);
    }
    function hang(isZap) {
      if (st.trial >= 3) {
        st.step = 7; st.app = OLD; st.trial = 0; st.stg = "";
        show("Ba lần không xong. Bootloader <b>lấy lại bản cũ</b> từ DỰ PHÒNG và vứt bản hỏng đi. Kit vẫn sống.");
        return;
      }
      st.trial++;
      show((isZap ? "Rút điện, bật lại: tính là một lần khởi động. " : "Treo, bị khởi động lại. ") + "Đây là <b>lần thử " + st.trial + " / 3</b>.");
    }
    function pull() {
      var s = st.step;
      if (s === 0) { show("Rút điện, bật lại: <b>bản cũ chạy</b>, chưa có gì thay đổi."); }
      if (s === 1) { st.stg = ""; st.step = 0; show("Rút điện, bật lại: <b>bản cũ chạy</b>. Phần nhận dở bị bỏ, gửi lại từ đầu."); }
      if (s === 2) { st.step = 3; st.bak = true; show("Rút điện, bật lại: tờ lệnh “cài” <b>vẫn còn</b>, nên bootloader làm tiếp: cất bản cũ sang DỰ PHÒNG."); }
      if (s === 3) { show("Rút điện, bật lại: bản cũ còn nguyên trong chip, nên bootloader <b>cất lại từ đầu</b>."); }
      if (s === 4) { show("Rút điện, bật lại: chỗ chạy chưa có tờ bìa nên không ai chạy nó. Bootloader <b>chép lại</b> từ ngăn CHỜ."); }
      if (s === 5) { hang(true); }
    }
    reset();
  })();

  /* ------------------------------------------------------------------ 5: folders */
  [
    ["src/app", "<b>Ứng dụng mẫu.</b> Người gác, dòng lệnh, nhận firmware mới. Dự án mới viết ở đây.", "1 170"],
    ["src/demo", "<b>Bản demo.</b> 17 màn hình, game, nhạc, video, máy ảo cho kéo thả.", "6 541"],
    ["src/services", "<b>Dịch vụ dùng chung.</b> Nạp firmware, Modbus, sổ sự cố, ghi log.", "2 458"],
    ["src/kernel", "<b>Kernel AK.</b> Vòng lặp chính, hộp thư, message, hẹn giờ. Mục 1 và 2 ở trên.", "1 865"],
    ["src/boot", "<b>Bootloader.</b> Kiểm, cài, quay lui. Mục 3 và 4 ở trên.", "535"],
    ["src/hal", "<b>Danh sách việc chip phải làm.</b> Ghi bộ nhớ, gửi chữ, bật đèn, đọc đồng hồ.", "275"],
    ["src/port", "<b>Phần riêng từng nơi chạy.</b> Kit STM32L151, chip STM32G071, máy tính, trình duyệt.", "5 560"],
    ["tests", "<b>Bộ kiểm tự động.</b> Chạy trên máy tính, kể cả cắt điện giữa lúc ghi.", "5 018"],
    ["tools", "<b>Công cụ trên máy tính.</b> Nạp qua USB và RS485, xem màn hình kit, tạo dự án mới.", ""]
  ].forEach(function (r) {
    var tr = document.createElement("tr");
    tr.innerHTML = '<td><a href="' + GH.replace("/blob/", "/tree/") + r[0] + '">' + r[0] + "/</a></td><td>" + r[1] + "</td><td>" + r[2] + "</td>";
    $("tree").appendChild(tr);
  });

  /* ------------------------------------------------------------------ 2: the messages of the running firmware */
  AkKitUI.create($("kit"), { caption: "AK Base Kit", sound: true }).then(function (kit) {
    var N = kit.names();
    var IRQ = N.AK_TASK_INTERRUPT_ID, IDLE = N.AK_TASK_IDLE_ID;
    var lanes = {}, counts = {};
    var paused = false, queue = [], slept = 0, picked = null, lastShown = 0;

    each($("board").querySelectorAll(".lane"), function (l) { lanes[N[l.dataset.id]] = l; counts[N[l.dataset.id]] = 0; });

    /* what each (task, signal) means; routine = part of the background rhythm */
    function sig(e) {
      var d = e.des, s = e.sig;
      if (s === N.AK_SIG_PING) { return { n: "ĐIỂM DANH", c: "AK_SIG_PING", f: "src/app/task_system.c", routine: true, p: "Người gác hỏi “còn sống không?”. Task không phải trả lời: chỉ cần nó nhận được message này là đủ biết nó còn được chạy." }; }
      if (d === N.TASK_TIMER_TICK_ID && s === N.TIMER_TICK) { return { n: "TỚI GIỜ", c: "TIMER_TICK", f: "src/kernel/src/timer.c", routine: true, p: "Đồng hồ phần cứng báo: có một hẹn giờ vừa đến hạn. Task Hẹn giờ xem đó là hẹn giờ của ai rồi gửi message cho task ấy." }; }
      if (d === N.TASK_SYSTEM_ID) {
        if (s === N.SYSTEM_SIG_INIT) { return { n: "KHỞI ĐỘNG", c: "SYSTEM_SIG_INIT", f: "src/app/task_system.c", p: "Người gác bắt đầu làm việc: đặt nhịp tim 1 giây và điểm danh các task lần đầu." }; }
        if (s === N.SYSTEM_SIG_HEARTBEAT) { return { n: "NHỊP TIM", c: "SYSTEM_SIG_HEARTBEAT", f: "src/app/task_system.c", routine: true, p: "Mỗi giây một lần: xem task nào im lặng quá lâu, báo cho bộ canh phần cứng là mọi thứ ổn, nháy đèn, rồi điểm danh lại." }; }
        if (s === N.SYSTEM_SIG_CONFIRM) { return { n: "CHỐT BẢN MỚI", c: "SYSTEM_SIG_CONFIRM", f: "src/app/task_system.c", p: "Bản firmware mới đã chạy đủ lâu mà không sao: chốt, khỏi quay lui." }; }
      }
      if (d === N.TASK_CONSOLE_ID) {
        if (s === N.CONSOLE_SIG_INIT) { return { n: "KHỞI ĐỘNG", c: "CONSOLE_SIG_INIT", f: "src/app/task_console.c", p: "Dòng lệnh sẵn sàng nhận chữ." }; }
        if (s === N.CONSOLE_SIG_LINE) { return { n: "MỘT DÒNG LỆNH", c: "CONSOLE_SIG_LINE", f: "src/app/task_console.c", p: "Vòng đi tuần thấy có người gõ xong một dòng và bấm Enter. Nó không tự xử lý: gói cả dòng vào một message gửi cho task Dòng lệnh. Task này tra lệnh và làm, thường là bằng cách gửi message cho task khác." }; }
      }
      if (d === N.TASK_FW_ID) {
        if (s === N.FW_SIG_RESET) { return { n: "XIN KHỞI ĐỘNG LẠI", c: "FW_SIG_RESET", f: "src/app/task_fw.c", p: "Có lệnh khởi động lại. Task không reset ngay: nó đặt một hẹn giờ ngắn để chữ đang in kịp gửi hết." }; }
        if (s === N.FW_SIG_DO_RESET) { return { n: "KHỞI ĐỘNG LẠI", c: "FW_SIG_DO_RESET", f: "src/app/task_fw.c", p: "Hẹn giờ đến hạn: chip khởi động lại. Mọi thứ bắt đầu lại từ bước 1 của mục 3." }; }
        if (s === N.FW_SIG_INSTALL) { return { n: "CÀI BẢN MỚI", c: "FW_SIG_INSTALL", f: "src/app/task_fw.c", p: "Bản mới đã nằm đủ trong ngăn CHỜ và đã kiểm: để lại tờ lệnh cho bootloader rồi khởi động lại." }; }
      }
      if (d === N.TASK_UI_ID) {
        if (s === N.UI_SIG_INIT) { return { n: "KHỞI ĐỘNG", c: "UI_SIG_INIT", f: "src/demo/task_ui.c", p: "Giao diện bật màn hình, mở menu và đặt hẹn giờ cho khung hình đầu tiên." }; }
        if (s === N.UI_SIG_FRAME) { return { n: "VẼ KHUNG", c: "UI_SIG_FRAME", f: "src/demo/task_ui.c", routine: true, p: "Vẽ một khung hình, gửi những dải màn hình đã đổi, rồi đặt hẹn giờ 50 ms cho khung sau. Không có vòng lặp chờ nào: giữa hai khung, task khác được chạy." }; }
        if (s === N.UI_SIG_KEY_1 || s === N.UI_SIG_KEY_2 || s === N.UI_SIG_KEY_3) {
          var k = s === N.UI_SIG_KEY_1 ? 1 : (s === N.UI_SIG_KEY_2 ? 2 : 3);
          return { n: "PHÍM B" + k, c: "UI_SIG_KEY_" + k, f: "src/demo/task_ui.c", p: "Nút B" + k + " vừa được bấm. Giao diện đưa phím cho màn hình đang mở; màn hình đổi trạng thái và kêu bíp. Hình mới sẽ hiện ở lần VẼ KHUNG kế tiếp." };
        }
        if (s === N.UI_SIG_BACK) { return { n: "VỀ MENU", c: "UI_SIG_BACK", f: "src/demo/task_ui.c", p: "Nút B3 được giữ lâu: đóng màn hình đang mở, quay về menu." }; }
        if (s === N.UI_SIG_BEEP_OFF) { return { n: "TẮT CÒI", c: "UI_SIG_BEEP_OFF", f: "src/demo/task_ui.c", p: "Lúc kêu bíp, giao diện bật còi rồi đặt một hẹn giờ. Bây giờ hẹn giờ đó đến hạn: tắt còi. Đây là cách “chờ” ở đây: không đứng đợi, nhận một message khi tới lúc." }; }
        if (s === N.UI_SIG_NOTE) { return { n: "NỐT KẾ", c: "UI_SIG_NOTE", f: "src/demo/music.c", p: "Nốt nhạc đang phát đã đủ dài: chuyển sang nốt kế tiếp và đặt hẹn giờ cho nốt đó." }; }
      }
      return { n: "tín hiệu " + s, c: "sig " + s, f: "src/app/app.h", p: "Một message chưa có lời giải thích ở trang này." };
    }
    function who(id) {
      var l = lanes[id];
      return l ? l.querySelector("b").textContent : "task " + id;
    }
    /* the first messages are posted by app_init(), before the loop runs */
    function atStart(e) {
      return e.src === IDLE && ((e.des === N.TASK_SYSTEM_ID && e.sig === N.SYSTEM_SIG_INIT) ||
        (e.des === N.TASK_CONSOLE_ID && e.sig === N.CONSOLE_SIG_INIT) || (e.des === N.TASK_UI_ID && e.sig === N.UI_SIG_INIT));
    }
    function from(e) { return atStart(e) ? "Lúc khởi động" : who(e.src); }
    function clock(ms) {
      var s = Math.floor(ms / 1000);
      return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60) + "." + ("00" + (ms % 1000)).slice(-3);
    }
    function origin(e) {
      if (atStart(e)) { return "Lúc khởi động, trước khi vòng lặp chạy, firmware bỏ sẵn message này vào hộp thư."; }
      if (e.src === IRQ) { return "Một ngắt phần cứng sinh ra message này."; }
      if (e.src === IDLE) { return "Vòng đi tuần sinh ra message này."; }
      if (e.src === N.TASK_TIMER_TICK_ID) { return "Một hẹn giờ đến hạn, nên task Hẹn giờ gửi message này."; }
      return "Task " + who(e.src) + " gửi message này trong lúc đang làm việc của nó.";
    }

    /* the arrow from the sender lane to the receiver lane */
    function arc(e) {
      var a = atStart(e) ? null : lanes[e.src], b = lanes[e.des], w = $("wire").clientWidth || 54, y1, y2, x = w - 6;
      each($("board").querySelectorAll(".lane"), function (l) { l.classList.remove("from", "to"); });
      if (b) { b.classList.add("to"); }
      if (!a || !b) { $("arc").setAttribute("d", ""); return; }
      a.classList.add("from");
      b.classList.add("to");
      y1 = a.offsetTop + a.offsetHeight / 2;
      y2 = b.offsetTop + b.offsetHeight / 2;
      if (a === b) { y1 -= 9; y2 += 9; }
      $("arc").setAttribute("d", "M" + x + "," + y1 + " C" + (x - w + 4) + "," + y1 + " " + (x - w + 4) + "," + y2 + " " + (x - 4) + "," + y2);
    }
    function show(e, nap) {
      var m = sig(e);
      arc(e);
      $("evT").textContent = from(e) + " → " + who(e.des) + ": " + m.n;
      $("evP").textContent = (nap ? "Trước đó không có việc gì: chip ngủ " + nap + " ms. " : "") + origin(e) + " " + m.p;
      $("evS").innerHTML = "Tên trong mã: <code>" + m.c + "</code> · " + link(m.f).replace("Trong mã nguồn: ", "xử lý ở ");
      lightStages((nap ? [3] : []).concat(e.src === IDLE && !atStart(e) ? [2, 0, 1] : [0, 1]));
    }
    function count(e) {
      var l = lanes[e.des], m = sig(e);
      counts[e.des] = (counts[e.des] || 0) + 1;
      if (l) {
        l.querySelector(".cnt").textContent = counts[e.des];
        l.querySelector(".last").textContent = m.n + (atStart(e) ? " · lúc khởi động" : " · từ " + who(e.src));
      }
    }
    function logLine(e, nap) {
      var m = sig(e), li = document.createElement("li"), b = document.createElement("button"), box = $("log");
      b.type = "button";
      b.setAttribute("aria-pressed", "false");
      b.innerHTML = "<time>" + clock(e.t) + "</time><span>" + from(e) + " → " + who(e.des) + " · " + m.n + "</span>";
      b.onclick = function () {
        each(box.querySelectorAll("button"), function (x) { x.setAttribute("aria-pressed", x === b ? "true" : "false"); });
        picked = e;
        show(e, nap);
      };
      li.appendChild(b);
      box.insertBefore(li, box.firstChild);
      while (box.children.length > 60) { box.removeChild(box.lastChild); }
    }

    /* running: take what the firmware did since the last look */
    function poll() {
      var evs, quiet = $("quiet").checked, shown = null, now = Date.now();
      if (paused) { return; }
      evs = kit.trace();
      evs.forEach(function (e) {
        var r = sig(e).routine;
        count(e);
        if (!r || !quiet) { logLine(e, 0); }
        if (!r) { shown = e; }
      });
      if (shown) { picked = null; lastShown = now; show(shown, 0); }
      else if (evs.length && !picked && now - lastShown > 1500) { lastShown = now - 900; show(evs[evs.length - 1], 0); }
    }
    setInterval(poll, 120);

    function setPaused(on) {
      paused = on;
      kit.pause(on);
      queue = [];
      slept = 0;
      picked = null;
      $("run").setAttribute("aria-pressed", on ? "false" : "true");
      $("stop").setAttribute("aria-pressed", on ? "true" : "false");
      $("step").disabled = !on;
      if (on) {
        kit.trace().forEach(count);
        $("evT").textContent = "Firmware đang dừng.";
        $("evP").textContent = "Thời gian của nó đứng yên: màn hình kit không đổi nữa. Bấm Bước tiếp để nó chạy tới message kế tiếp rồi dừng lại.";
        $("evS").textContent = "";
        lightStages([]);
      }
    }
    $("run").onclick = function () { setPaused(false); };
    $("stop").onclick = function () { setPaused(true); };
    $("step").onclick = function () {
      var e, nap = 0, tries = 0, quiet = $("quiet").checked;
      for (;;) {
        if (!queue.length) {
          slept += kit.step();
          queue = kit.trace();
          if (!queue.length) { break; }                 /* half a second and nothing: show the pause */
        }
        e = queue.shift();
        count(e);
        if (!quiet || !sig(e).routine) { break; }
        e = null;
        if (++tries > 80) { break; }
      }
      if (!e) {
        $("evT").textContent = "Chưa có việc gì mới.";
        $("evP").textContent = "Firmware đã chạy thêm " + slept + " ms mà không có message nào ngoài việc lặp lại. Tạo một việc ở dưới, hoặc bỏ dấu “Ẩn việc lặp lại”.";
        $("evS").textContent = "";
        slept = 0;
        return;
      }
      nap = queue.length || quiet ? 0 : Math.max(0, slept - 1);
      logLine(e, nap);
      show(e, nap);
      slept = 0;
    };

    each(document.querySelectorAll("[data-cmd]"), function (b) { b.onclick = function () { kit.send(b.dataset.cmd); note(); }; });
    each(document.querySelectorAll("[data-open]"), function (b) { b.onclick = function () { kit.open(b.dataset.open); note(); }; });
    function note() {
      if (paused) {
        $("evT").textContent = "Đã tạo việc. Firmware vẫn đang dừng.";
        $("evP").textContent = "Việc vừa tạo đang nằm chờ. Bấm Bước tiếp để xem nó được xử lý thế nào.";
        $("evS").textContent = "";
      }
    }
    $("reboot").onclick = function () {
      setPaused(true);
      $("quiet").checked = false;
      kit.send("reboot");
      $("evT").textContent = "Đã gửi lệnh khởi động lại. Firmware đang dừng.";
      $("evP").textContent = "Bấm Bước tiếp nhiều lần: dòng lệnh nhận lệnh, task Nạp firmware đặt hẹn giờ, chip khởi động lại, rồi các message KHỞI ĐỘNG đầu tiên lần lượt hiện ra.";
      $("evS").textContent = "";
      $("msg").scrollIntoView({ behavior: "smooth" });
    };

    kit.open("clock");
  });
})();
