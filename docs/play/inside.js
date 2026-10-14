/*
 * inside.js - the page "Bên trong firmware" (inside.html): one running kit and
 * a handful of things to play with around it.
 *
 *   01  a button press lights the five stops a key message passes; the strip
 *       of display pages is read from the running firmware (shell command "ui")
 *   02  a drawing pad: which of the eight display pages a change touches
 *   03  three mailboxes and a dispatcher that always serves the highest priority
 *   04  an update with backup, trial and rollback, as a simulation in the order
 *       the bootloader works (boot_core.c); "pull the plug" at any step
 *   05  a block program compiled here and run on the kit of this page
 *   06  shell commands sent to the kit of this page, with its real answer
 *   07  what changes when the firmware moves to another chip
 *   08  RAM used by three builds (numbers printed by the linker, v1.3.9)
 */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var each = function (list, fn) { Array.prototype.forEach.call(list, fn); };
  var press = function (box, hit) { each(box.children, function (b) { b.setAttribute("aria-pressed", b === hit ? "true" : "false"); }); };
  var COLORS = ["#a8ff3e", "#3ee0ff", "#a493ff", "#ffb43e", "#ff6aa9"];
  var examples = window.AkBlocksExamples || [];

  /* ------------------------------------------------------------------ 02: drawing pad */
  (function () {
    var pad = $("pad"), g = pad.getContext("2d"), cells = $("vs").children;
    var dirty = 0, down = false, last = null, fade = null;
    g.fillStyle = "#000"; g.fillRect(0, 0, 128, 64);
    function show(mask, text) {
      var n = 0;
      each(cells, function (c, k) { var on = !!(mask & (1 << k)); c.className = on ? "on" : ""; n += on ? 1 : 0; });
      $("padms").textContent = (n * 5) + " ms / 50";
      $("padbar").style.width = (n * 10) + "%";
      $("padm").className = "meter" + (n === 8 ? " hot" : "");
      if (text !== undefined) { $("padsay").textContent = text; }
      else if (n) { $("padsay").textContent = "Phải gửi " + n + " / 8 dải"; }
    }
    function at(e) {
      var r = pad.getBoundingClientRect();
      return [Math.max(0, Math.min(127, Math.floor((e.clientX - r.left) * 128 / r.width))),
              Math.max(0, Math.min(63, Math.floor((e.clientY - r.top) * 64 / r.height)))];
    }
    function dot(x, y) {
      g.fillStyle = "#e9fbff";
      g.fillRect(x - 1, y - 1, 3, 3);
      for (var p = Math.max(0, y - 1) >> 3; p <= Math.min(63, y + 1) >> 3; p++) { dirty |= 1 << p; }
    }
    function line(a, b) {
      var n = Math.max(Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1]), 1);
      for (var k = 0; k <= n; k++) { dot(Math.round(a[0] + (b[0] - a[0]) * k / n), Math.round(a[1] + (b[1] - a[1]) * k / n)); }
    }
    function settle() { clearTimeout(fade); fade = setTimeout(function () { dirty = 0; show(0, "Không có gì đổi: không gửi gì cả"); }, 1400); }
    pad.addEventListener("pointerdown", function (e) { e.preventDefault(); pad.setPointerCapture(e.pointerId); down = true; dirty = 0; last = at(e); dot(last[0], last[1]); show(dirty); clearTimeout(fade); });
    pad.addEventListener("pointermove", function (e) { if (!down) { return; } var p = at(e); line(last, p); last = p; show(dirty); });
    pad.addEventListener("pointerup", function () { down = false; settle(); });
    pad.addEventListener("pointercancel", function () { down = false; settle(); });
    $("padfill").onclick = function () { g.fillStyle = "#e9fbff"; g.fillRect(0, 0, 128, 64); dirty = 255; show(255, "Cả 8 dải đều đổi: sát ngân sách"); settle(); };
    $("padclear").onclick = function () { g.fillStyle = "#000"; g.fillRect(0, 0, 128, 64); dirty = 255; show(255, "Xoá cũng là đổi cả 8 dải"); settle(); };
    show(0);
  })();

  /* ------------------------------------------------------------------ 03: mailboxes */
  (function () {
    var boxes = $("boxes").children, q = [0, 0, 0], busy = -1, MAX = 9;
    function draw() {
      each(boxes, function (b, k) {
        var holder = b.lastElementChild;
        while (holder.children.length > q[k]) { holder.removeChild(holder.lastChild); }
        while (holder.children.length < q[k]) { holder.appendChild(document.createElement("i")); }
        b.classList.toggle("busy", k === busy);
      });
    }
    each(boxes, function (b, k) { b.onclick = function () { if (q[k] < MAX) { q[k]++; draw(); } }; });
    setInterval(function () { if (!document.hidden && q[2] < 3) { q[2]++; draw(); } }, 1700);      /* the frame timer */
    setInterval(function () {
      if (document.hidden) { return; }
      busy = q[0] ? 0 : (q[1] ? 1 : (q[2] ? 2 : -1));
      if (busy < 0) { $("now").textContent = "Đang rảnh: chip ngủ chờ việc."; draw(); return; }
      q[busy]--;
      $("now").innerHTML = "Đang làm: <b>" + boxes[busy].dataset.n + "</b>" + (busy < 2 && q[2] ? " · vẽ màn hình phải chờ" : "");
      draw();
    }, 800);
    draw();
  })();

  /* ------------------------------------------------------------------ 04: update */
  (function () {
    var OLD = "bản cũ", NEW = "bản mới", st;
    function tank(id, cls, pct, text, run) {
      var t = $(id);
      t.className = "tank " + cls + (run ? " run" : "");
      t.firstChild.style.width = pct + "%";
      t.lastChild.textContent = text;
    }
    function say(html, zap) { $("say").className = "say" + (zap ? " zap" : ""); $("say").innerHTML = html; }
    function acts(list) {
      var box = $("acts");
      box.textContent = "";
      list.forEach(function (a) {
        var b = document.createElement("button");
        b.type = "button";
        b.className = "act " + (a[2] || "");
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
    var zap = ["⚡ Rút điện", function () { pull(); }, "zap"];
    function draw() {
      var s = st.step;
      tank("tApp", s === 4 ? "half" : (st.app === NEW ? "new" : "old"), s === 4 ? 55 : 100,
           s === 4 ? "đang chép… chưa chạy được" : st.app + (st.trial ? " · chạy thử lần " + st.trial + " / 3" : " · đang chạy"), s !== 4);
      tank("tStg", st.stg === "" ? "" : (st.stg === "half" ? "half" : "new"), st.stg === "" ? 0 : (st.stg === "half" ? 60 : 100),
           st.stg === "" ? "trống" : (st.stg === "half" ? "đang nhận…" : NEW + (s >= 2 ? " · đã kiểm" : "")));
      tank("tBak", st.bak ? "old" : "", st.bak ? 100 : 0, st.bak ? OLD + " · cất giữ" : "trống");
    }
    function show(text, isZap) {
      var s = st.step;
      draw();
      say(text, isZap);
      if (s === 5) {
        acts([["Chạy tốt 30 giây ✓", function () { st.step = 6; st.trial = 0; show("<b>Chốt.</b> Bản mới ở lại. Bản cũ vẫn nằm trong DỰ PHÒNG, phòng khi cần."); }, "go"],
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
        show("Ba lần không xong. Bootloader <b>lấy lại bản cũ</b> từ DỰ PHÒNG và vứt bản hỏng đi. Kit vẫn sống.", isZap);
        return;
      }
      st.trial++;
      show((isZap ? "Bật lại: tính là một lần khởi động. " : "Treo, bị khởi động lại. ") + "Đây là <b>lần thử " + st.trial + " / 3</b>.", isZap);
    }
    function pull() {
      var s = st.step, slots = $("slots");
      slots.classList.remove("flash");
      void slots.offsetWidth;
      slots.classList.add("flash");
      if (s === 0) { show("Bật lại: <b>bản cũ chạy</b>, chưa có gì thay đổi.", true); }
      if (s === 1) { st.stg = ""; st.step = 0; show("Bật lại: <b>bản cũ chạy</b>. Phần nhận dở bị bỏ, gửi lại từ đầu.", true); }
      if (s === 2) { st.step = 3; st.bak = true; show("Bật lại: tờ lệnh “cài” <b>vẫn còn</b>, nên bootloader làm tiếp: cất bản cũ sang DỰ PHÒNG.", true); }
      if (s === 3) { show("Bật lại: bản cũ còn nguyên trong chip, nên bootloader <b>cất lại từ đầu</b>.", true); }
      if (s === 4) { show("Bật lại: chỗ chạy chưa có tờ bìa nên không ai chạy nó. Bootloader <b>chép lại</b> từ ngăn CHỜ.", true); }
      if (s === 5) { hang(true); }
    }
    reset();
  })();

  /* ------------------------------------------------------------------ 07: another chip */
  (function () {
    var PORTS = [
      ["Kit này", "STM32L151: chân, bộ nhớ, màn hình của kit", "3 705 dòng"],
      ["Chip khác", "STM32G071: đã chạy trong trình giả lập, chưa có bo thật", "974 dòng"],
      ["Máy tính", "bộ nhớ là file trên đĩa: thử cắt điện hàng chục nghìn lần", "881 dòng"],
      ["Trình duyệt", "chính là kit trên trang này", "337 dòng"]
    ];
    function pick(b) {
      var p = PORTS[+b.dataset.k], row = $("port");
      press($("chips"), b);
      $("portn").textContent = p[0]; $("portd").textContent = p[1]; $("portl").textContent = "viết lại · " + p[2];
      row.classList.remove("pulse"); void row.offsetWidth; row.classList.add("pulse");
    }
    each($("chips").children, function (b) { b.onclick = function () { pick(b); }; });
    pick($("chips").children[0]);
  })();

  /* ------------------------------------------------------------------ 08: RAM */
  (function () {
    function pick(b) {
      var n = +b.dataset.b, pct = Math.round(n * 100 / 16384);
      press($("rams"), b);
      $("ramv").textContent = String(n).replace(/\B(?=(\d{3})+$)/g, " ") + " / 16 384 byte · " + pct + " %";
      $("ramb").style.width = pct + "%";
      $("ramm").className = "meter" + (pct > 70 ? " hot" : "");
    }
    each($("rams").children, function (b) { b.onclick = function () { pick(b); }; });
    pick($("rams").children[1]);
  })();

  /* ------------------------------------------------------------------ the kit: 01, 05, 06 */
  var stops = $("path").children, timers = [];
  function travel() {
    timers.forEach(clearTimeout);
    timers = [];
    each(stops, function (s, k) {
      s.classList.remove("on");
      timers.push(setTimeout(function () { s.classList.add("on"); }, k * 150));
      timers.push(setTimeout(function () { s.classList.remove("on"); }, k * 150 + 520));
    });
    $("nudge").textContent = "Mỗi lần bấm là một tờ phiếu đi hết năm chặng.";
    $("nudge").className = "nudge done";
  }
  function markScreen(name) {
    var hit = null;
    each($("screens").children, function (b) { if (b.dataset.s === name) { hit = b; } });
    press($("screens"), hit);
    $("ms").textContent = hit ? hit.dataset.ms : "–";
    $("msbar").style.width = (hit ? hit.dataset.p : 0) + "%";
    $("msm").className = "meter" + (hit && +hit.dataset.p > 90 ? " hot" : "");
  }
  var MEANS = {
    stat: function (t) { var m = /pure\s+(\d+)\s+(\d+)/.exec(t); return m ? "Đang có " + m[1] + " phiếu chờ. Lúc đông nhất từng có " + m[2] + " phiếu cùng lúc." : ""; },
    crash: function (t) { return /empty/.test(t) ? "Chưa lần nào. Sổ sự cố trống." : "Có: mỗi dòng là một lần, kèm việc đang làm lúc đó."; },
    ver: function (t) { var m = /app v([\d.]+)/.exec(t); return m ? "Bản " + m[1] + ", đúng mã nguồn trên GitHub." : ""; },
    info: function (t) { return /app\s+0x\w+\s+\d+ B\s+no image/.test(t) ? "Trên trình duyệt các ngăn đều trống: firmware chạy thẳng, không qua nạp. Trên kit thật, mỗi dòng ghi bản đang nằm trong ngăn đó." : "Mỗi dòng là một ngăn nhớ và bản firmware đang nằm trong đó."; },
    blk: function (t) { var m = /(\d+) of 768/.exec(t); return m && +m[1] ? "Đang giữ một chương trình " + m[1] + " byte." : "Chưa có. Chạy một chương trình ở mục 05 rồi hỏi lại."; }
  };

  AkKitUI.create($("kit"), { caption: "AK Base Kit", sound: true }).then(function (kit) {
    var cells = $("pages").children, peak = 0, peakAt = 0;
    var line = "", asking = null, got = [], askTimer = null;

    kit.element.querySelector(".akkit-buttons").addEventListener("pointerdown", travel);
    document.addEventListener("keydown", function (e) { if (!e.repeat && (e.key === "1" || e.key === "2" || e.key === "3")) { travel(); } });

    function strip(n) {
      var now = Date.now();
      if (n >= peak || now - peakAt > 900) { peak = n; peakAt = now; }          /* hold a burst long enough to see */
      $("sent").textContent = peak + " / 8 dải";
      each(cells, function (c, k) { c.className = k < peak ? "on" : ""; });
    }
    function gotLine(s) {
      var m = /(\d+) of \d+ pages sent/.exec(s);
      s = s.replace(/^[> ]+/, "").trim();
      if (m) { strip(+m[1]); return; }
      if (!asking || !s || s === "ui" || s === asking) { return; }
      got.push(s);
    }
    kit.onConsole = function (text) {
      var parts = (line + text).split("\n");
      line = parts.pop();
      parts.forEach(gotLine);
    };
    setInterval(function () { if (!document.hidden && !asking) { kit.send("ui"); } }, 180);

    each($("asks").children, function (b) {
      b.onclick = function () {
        var cmd = b.dataset.c;
        press($("asks"), b);
        clearTimeout(askTimer);
        asking = cmd; got = [];
        $("tout").textContent = "> " + cmd;
        $("tsay").textContent = "…";
        kit.send(cmd);
        askTimer = setTimeout(function () {
          var text = got.join("\n");
          $("tout").textContent = "> " + cmd + "\n" + (text || "(không trả lời)");
          $("tsay").textContent = MEANS[cmd](text) || "Firmware đã trả lời ở trên.";
          asking = null;
        }, 450);
      };
    });

    each($("screens").children, function (b) {
      b.onclick = function () {
        kit.open(b.dataset.s);
        kit.autoplay(true);
        markScreen(b.dataset.s);
        press($("progs"), null);
      };
    });

    ["counter", "ball", "clock", "blink"].forEach(function (id) {
      var ex = examples.filter(function (x) { return x.id === id; })[0], b;
      if (!ex) { return; }
      b = document.createElement("button");
      b.type = "button";
      b.className = "chip";
      b.textContent = ex.name;
      b.setAttribute("aria-pressed", "false");
      b.onclick = function () {
        var r = AkBlocks.compile(ex.workspace), box = $("hex");
        if (!r.ok) { return; }
        press($("progs"), b);
        $("nbytes").textContent = r.bytes.length;
        box.textContent = "";
        each(r.bytes, function (v, k) {
          var c = document.createElement("i");
          c.textContent = (v < 16 ? "0" : "") + v.toString(16).toUpperCase();
          c.style.background = COLORS[v % COLORS.length];
          c.style.animationDelay = Math.min(k * 6, 600) + "ms";
          box.appendChild(c);
        });
        $("about").textContent = ex.about;
        $("edit").href = "blocks.html?ex=" + ex.id;
        $("edit").textContent = "Tự kéo thả chương trình này →";
        kit.runBlocks(r.bytes);
        markScreen("");
      };
      $("progs").appendChild(b);
    });

    kit.open("clock");
    markScreen("clock");
  });
})();
