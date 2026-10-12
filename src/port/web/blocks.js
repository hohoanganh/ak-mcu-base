/*
 * blocks.js - the block editor page (blocks.html): the blocks of the kit, the
 * toolbox, and the three things one can do with a program: run it on the kit
 * in the page, send it to a real kit over USB (Web Serial), start from an
 * example.
 *
 * Blockly draws the blocks; blocks-compiler.js turns them into the bytecode
 * the firmware runs (src/demo/blk_vm.h). Nothing is compiled or flashed.
 */
(function () {
  "use strict";

  var $ = function (id) { return document.getElementById(id); };
  var HUE = { control: 45, kit: 160, screen: 200, logic: 265, math: 230, vars: 330 };
  var STORE_KEY = "ak-blocks-workspace-v1";

  /* ---------------------------------------------------------------- blocks */
  var NUM = { type: "input_value", check: ["Number", "Boolean"] };
  function arg(name) { return Object.assign({ name: name }, NUM); }
  var SIZES = [["nhỏ", "1"], ["vừa", "2"], ["lớn", "3"], ["rất lớn", "4"]];
  var BUTTONS = [["B1", "1"], ["B2", "2"], ["B3", "3"]];

  Blockly.defineBlocksWithJsonArray([
    { type: "ak_start", message0: "khi bắt đầu", nextStatement: null, colour: HUE.control,
      tooltip: "Chương trình là những khối nối dưới khối này. Khối nằm rời không được chạy." },
    { type: "ak_forever", message0: "lặp mãi %1 %2", args0: [{ type: "input_dummy" }, { type: "input_statement", name: "DO" }],
      previousStatement: null, colour: HUE.control, tooltip: "Làm đi làm lại các khối bên trong, không bao giờ dừng." },
    { type: "ak_wait", message0: "chờ %1 mili giây", args0: [arg("MS")], inputsInline: true,
      previousStatement: null, nextStatement: null, colour: HUE.control, tooltip: "1000 mili giây = 1 giây." },

    { type: "ak_led", message0: "đèn LED %1", args0: [{ type: "field_dropdown", name: "STATE", options: [["bật", "ON"], ["tắt", "OFF"]] }],
      previousStatement: null, nextStatement: null, colour: HUE.kit, tooltip: "Đèn đỏ trên kit." },
    { type: "ak_beep", message0: "còi kêu %1 Hz trong %2 mili giây", args0: [arg("HZ"), arg("MS")], inputsInline: true,
      previousStatement: null, nextStatement: null, colour: HUE.kit, tooltip: "Số Hz càng lớn tiếng càng cao. Chương trình chạy tiếp ngay, không chờ còi kêu xong." },
    { type: "ak_button", message0: "nút %1 đang được giữ", args0: [{ type: "field_dropdown", name: "BTN", options: BUTTONS }],
      output: "Boolean", colour: HUE.kit, tooltip: "Đúng trong lúc ngón tay còn đè trên nút." },
    { type: "ak_pressed", message0: "nút %1 vừa được bấm", args0: [{ type: "field_dropdown", name: "BTN", options: BUTTONS }],
      output: "Boolean", colour: HUE.kit, tooltip: "Đúng một lần cho mỗi lần bấm. Giữ B3 lâu là thoát về menu của kit." },
    { type: "ak_temp", message0: "nhiệt độ (°C)", output: "Number", colour: HUE.kit, tooltip: "Từ cảm biến SHT45 ở cổng I2C. Không có cảm biến thì là 0." },
    { type: "ak_humi", message0: "độ ẩm (%)", output: "Number", colour: HUE.kit, tooltip: "Từ cảm biến SHT45 ở cổng I2C." },
    { type: "ak_time", message0: "%1", args0: [{ type: "field_dropdown", name: "WHAT",
        options: [["giờ", "HOUR"], ["phút", "MINUTE"], ["giây", "SECOND"], ["số giây từ lúc bắt đầu", "UPTIME"]] }],
      output: "Number", colour: HUE.kit, tooltip: "Giờ, phút, giây của đồng hồ trên kit." },

    { type: "ak_cls", message0: "xoá màn hình", previousStatement: null, nextStatement: null, colour: HUE.screen },
    { type: "ak_text", message0: "viết %1 tại x %2 y %3 cỡ %4",
      args0: [{ type: "field_input", name: "TEXT", text: "Xin chao" }, arg("X"), arg("Y"), { type: "field_dropdown", name: "SIZE", options: SIZES }],
      inputsInline: true, previousStatement: null, nextStatement: null, colour: HUE.screen,
      tooltip: "Màn hình rộng 128, cao 64 điểm; x = 0 là mép trái, y = 0 là mép trên. Chữ không dấu, tối đa 21 ký tự." },
    { type: "ak_number", message0: "viết số %1 tại x %2 y %3 cỡ %4",
      args0: [arg("VALUE"), arg("X"), arg("Y"), { type: "field_dropdown", name: "SIZE", options: SIZES }],
      inputsInline: true, previousStatement: null, nextStatement: null, colour: HUE.screen },
    { type: "ak_pixel", message0: "điểm tại x %1 y %2 %3",
      args0: [arg("X"), arg("Y"), { type: "field_dropdown", name: "STATE", options: [["sáng", "ON"], ["tắt", "OFF"]] }],
      inputsInline: true, previousStatement: null, nextStatement: null, colour: HUE.screen },
    { type: "ak_line", message0: "đường thẳng từ x %1 y %2 tới x %3 y %4", args0: [arg("X0"), arg("Y0"), arg("X1"), arg("Y1")],
      inputsInline: true, previousStatement: null, nextStatement: null, colour: HUE.screen },
    { type: "ak_rect", message0: "hình chữ nhật %1 tại x %2 y %3 rộng %4 cao %5",
      args0: [{ type: "field_dropdown", name: "STYLE", options: [["viền", "OUTLINE"], ["tô đặc", "FILLED"], ["xoá vùng", "ERASE"]] },
              arg("X"), arg("Y"), arg("W"), arg("H")],
      inputsInline: true, previousStatement: null, nextStatement: null, colour: HUE.screen },

    { type: "ak_minmax", message0: "số %1 trong %2 và %3",
      args0: [{ type: "field_dropdown", name: "OP", options: [["nhỏ hơn", "MIN"], ["lớn hơn", "MAX"]] }, arg("A"), arg("B")],
      inputsInline: true, output: "Number", colour: HUE.math }
  ]);

  function shadow(v) { return { shadow: { type: "math_number", fields: { NUM: v } } }; }
  function withShadows(type, values, fields) {
    var b = { kind: "block", type: type, inputs: {} };
    Object.keys(values).forEach(function (k) { b.inputs[k] = shadow(values[k]); });
    if (fields) { b.fields = fields; }
    return b;
  }

  var TOOLBOX = {
    kind: "categoryToolbox",
    contents: [
      { kind: "category", name: "Điều khiển", colour: HUE.control, contents: [
        { kind: "block", type: "ak_start" },
        { kind: "block", type: "ak_forever" },
        withShadows("controls_repeat_ext", { TIMES: 10 }),
        { kind: "block", type: "controls_if" },
        { kind: "block", type: "controls_if", extraState: { hasElse: true } },
        { kind: "block", type: "controls_whileUntil" },
        withShadows("ak_wait", { MS: 500 })
      ] },
      { kind: "category", name: "Kit", colour: HUE.kit, contents: [
        { kind: "block", type: "ak_led" },
        withShadows("ak_beep", { HZ: 1000, MS: 100 }),
        { kind: "block", type: "ak_button" },
        { kind: "block", type: "ak_pressed" },
        { kind: "block", type: "ak_temp" },
        { kind: "block", type: "ak_humi" },
        { kind: "block", type: "ak_time" }
      ] },
      { kind: "category", name: "Màn hình", colour: HUE.screen, contents: [
        { kind: "block", type: "ak_cls" },
        withShadows("ak_text", { X: 0, Y: 0 }),
        withShadows("ak_number", { VALUE: 0, X: 0, Y: 16 }, { SIZE: "2" }),
        withShadows("ak_pixel", { X: 64, Y: 32 }),
        withShadows("ak_line", { X0: 0, Y0: 0, X1: 127, Y1: 63 }),
        withShadows("ak_rect", { X: 10, Y: 10, W: 40, H: 20 })
      ] },
      { kind: "category", name: "Logic", colour: HUE.logic, contents: [
        withShadows("logic_compare", { A: 0, B: 0 }),
        { kind: "block", type: "logic_operation" },
        { kind: "block", type: "logic_negate" },
        { kind: "block", type: "logic_boolean" }
      ] },
      { kind: "category", name: "Toán", colour: HUE.math, contents: [
        { kind: "block", type: "math_number", fields: { NUM: 0 } },
        withShadows("math_arithmetic", { A: 1, B: 1 }),
        withShadows("math_modulo", { DIVIDEND: 10, DIVISOR: 3 }),
        withShadows("math_random_int", { FROM: 1, TO: 6 }),
        withShadows("ak_minmax", { A: 0, B: 0 })
      ] },
      { kind: "category", name: "Biến", colour: HUE.vars, custom: "VARIABLE" }
    ]
  };

  /* ------------------------------------------------------------- workspace */
  var theme = Blockly.Theme.defineTheme("ak-dark", {
    base: Blockly.Themes.Classic,
    componentStyles: {
      workspaceBackgroundColour: "#0e141b", toolboxBackgroundColour: "#111821", toolboxForegroundColour: "#d9e1ea",
      flyoutBackgroundColour: "#16202b", flyoutForegroundColour: "#d9e1ea", flyoutOpacity: 1,
      scrollbarColour: "#2c3d50", scrollbarOpacity: 0.7, insertionMarkerColour: "#ffffff", insertionMarkerOpacity: 0.3,
      cursorColour: "#a8ff3e"
    },
    fontStyle: { family: "system-ui, 'Segoe UI', sans-serif", size: 11 }
  });

  var workspace = Blockly.inject("blockly", {
    toolbox: TOOLBOX, theme: theme, media: "vendor/blockly/media/", sounds: false, trashcan: true,
    grid: { spacing: 24, length: 2, colour: "#1c2836", snap: true },
    zoom: { controls: true, wheel: true, startScale: 0.9, maxScale: 2, minScale: 0.4, scaleSpeed: 1.1 },
    move: { scrollbars: true, drag: true, wheel: false }
  });

  var examples = window.AkBlocksExamples || [];
  var kit = null;
  var compiled = null;
  var marked = [];

  function status(text, kind) {
    var s = $("status");
    s.textContent = text;
    s.className = "status" + (kind ? " " + kind : "");
  }

  function hexDump(bytes) {
    var out = [];
    for (var i = 0; i < bytes.length; i += 16) {
      out.push(Array.prototype.map.call(bytes.subarray(i, i + 16), function (b) { return ("0" + b.toString(16).toUpperCase()).slice(-2); }).join(" "));
    }
    return out.join("\n");
  }

  /* Compiles what is on the workspace and shows the result next to it. */
  function build() {
    var result = AkBlocks.compile(Blockly.serialization.workspaces.save(workspace));

    marked.forEach(function (id) { var b = workspace.getBlockById(id); if (b) { b.setWarningText(null); } });
    marked = [];
    result.errors.concat(result.warnings).forEach(function (e) {
      var b = e.blockId && workspace.getBlockById(e.blockId);
      if (b) { b.setWarningText(e.message); marked.push(e.blockId); }
    });

    $("size").textContent = result.bytes.length + " / " + AkBlocks.MAX_SIZE + " byte";
    $("hex").textContent = hexDump(result.bytes);
    $("run").disabled = !result.ok;
    $("usb").disabled = !result.ok;
    if (!result.ok) {
      status(result.errors[0].message, "bad");
    }
    else if (result.warnings.length) {
      status(result.warnings[0].message, "warn");
    }
    else {
      status("Sẵn sàng. Bấm Chạy để thử trên kit bên cạnh.", "");
    }
    compiled = result;
    return result;
  }

  function save() {
    try { localStorage.setItem(STORE_KEY, JSON.stringify(Blockly.serialization.workspaces.save(workspace))); }
    catch (e) { /* private window, storage blocked: the page works without it */ }
  }

  function load(state) {
    Blockly.Events.disable();
    try {
      workspace.clear();
      Blockly.serialization.workspaces.load(state, workspace);
    }
    finally { Blockly.Events.enable(); }
    /* the whole program in view, but never magnified */
    workspace.zoomToFit();
    if (workspace.getScale() > 0.9) { workspace.setScale(0.9); }
    workspace.scrollCenter();
    build();
  }

  function loadExample(ex) {
    /* a copy: Blockly keeps and changes the object it is given */
    load(JSON.parse(JSON.stringify(ex.workspace)));
    $("about").textContent = ex.about;
    save();
  }

  var timer = 0;
  workspace.addChangeListener(function (e) {
    if (e.isUiEvent) { return; }
    clearTimeout(timer);
    timer = setTimeout(function () { build(); save(); }, 250);
  });

  /* ------------------------------------------------- run on the kit in the page */
  function run() {
    var r = build();
    if (!r.ok || !kit) { return; }
    var err = kit.runBlocks(r.bytes);
    if (err) { status("Kit không nhận chương trình (mã " + err + ").", "bad"); return; }
    status("Đang chạy trên kit bên cạnh: " + r.bytes.length + " byte. Bấm B1, B2, B3 để thử nút.", "ok");
  }

  $("run").addEventListener("click", run);
  $("stop").addEventListener("click", function () {
    if (kit) { kit.send("blk stop"); status("Đã dừng.", ""); }
  });
  $("clear").addEventListener("click", function () {
    load({ blocks: { languageVersion: 0, blocks: [{ type: "ak_start", x: 30, y: 30 }] } });
    $("about").textContent = "Trang trắng: kéo khối từ các nhóm bên trái và nối vào dưới \"khi bắt đầu\".";
    save();
  });

  var pick = $("examples");
  examples.forEach(function (ex) {
    var o = document.createElement("option");
    o.value = ex.id;
    o.textContent = ex.name;
    pick.appendChild(o);
  });
  pick.addEventListener("change", function () {
    var ex = examples.filter(function (x) { return x.id === pick.value; })[0];
    if (ex) { loadExample(ex); run(); }
    pick.value = "";
  });

  /* ------------------------------------------------ send to a real kit (USB) */
  var FW_ERR = { 1: "tham số sai", 9: "chương trình hỏng trên đường truyền", 10: "lệch vị trí", 11: "firmware của kit chưa có tính năng này", 12: "chương trình quá dài" };

  function frame(cmd, seq, payload) {
    var body = new Uint8Array(4 + payload.length);
    body[0] = cmd; body[1] = seq; body[2] = payload.length & 0xFF; body[3] = payload.length >> 8;
    body.set(payload, 4);
    var crc = AkBlocks.crc16(body);
    var out = new Uint8Array(1 + body.length + 2);
    out[0] = 0xA5;
    out.set(body, 1);
    out[out.length - 2] = crc & 0xFF;
    out[out.length - 1] = crc >> 8;
    return out;
  }

  /* first complete response frame in buf (a plain array of bytes), or null;
   * console text around it is skipped */
  function takeFrame(buf) {
    for (var i = buf.indexOf(0xA5); i >= 0; i = buf.indexOf(0xA5, i + 1)) {
      if (buf.length - i < 7) { return null; }
      var len = buf[i + 3] | (buf[i + 4] << 8);
      if (len > 512) { continue; }
      if (buf.length - i < 7 + len) { continue; }
      var body = buf.slice(i + 1, i + 5 + len);
      if (AkBlocks.crc16(body) === (buf[i + 5 + len] | (buf[i + 6 + len] << 8)) && (body[0] & 0x80)) {
        buf.splice(0, i + 7 + len);
        return { cmd: body[0] & 0x7F, seq: body[1], payload: body.slice(4) };
      }
    }
    return null;
  }

  async function sendToKit(bytes) {
    var port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    var writer = port.writable.getWriter();
    var reader = port.readable.getReader();
    var buf = [], seq = 0, closed = false;

    (async function pump() {
      try {
        for (;;) {
          var r = await reader.read();
          if (r.done) { break; }
          for (var i = 0; i < r.value.length; i++) { buf.push(r.value[i]); }
          if (buf.length > 8192) { buf.splice(0, buf.length - 4096); }
        }
      }
      catch (e) { /* the port went away: the request below times out */ }
      closed = true;
    }());

    async function request(cmd, payload, what) {
      for (var attempt = 0; attempt < 3; attempt++) {
        seq = (seq + 1) & 0xFF;
        await writer.write(frame(cmd, seq, payload));
        var until = Date.now() + 1500;
        while (Date.now() < until && !closed) {
          var f = takeFrame(buf);
          if (f && f.cmd === cmd && f.seq === seq) {
            if (f.payload[0] !== 0) { throw new Error(what + ": " + (FW_ERR[f.payload[0]] || "lỗi " + f.payload[0])); }
            return f.payload;
          }
          await new Promise(function (ok) { setTimeout(ok, 10); });
        }
      }
      throw new Error("Kit không trả lời (" + what + "). Kit có đang chạy bản demo mới không?");
    }

    try {
      await request(0x48, new Uint8Array([bytes.length & 0xFF, bytes.length >> 8]), "bắt đầu");
      for (var off = 0; off < bytes.length; off += 96) {
        var part = bytes.subarray(off, off + 96);
        var p = new Uint8Array(2 + part.length);
        p[0] = off & 0xFF; p[1] = off >> 8;
        p.set(part, 2);
        await request(0x49, p, "gửi dữ liệu");
      }
      var crc = AkBlocks.crc16(bytes);
      await request(0x4A, new Uint8Array([crc & 0xFF, crc >> 8]), "kiểm tra");
    }
    finally {
      try { await reader.cancel(); } catch (e) { /* already closed */ }
      try { reader.releaseLock(); writer.releaseLock(); await port.close(); } catch (e) { /* already closed */ }
    }
  }

  if (!("serial" in navigator)) {
    $("usb").hidden = true;
    $("usb-note").textContent = "Gửi sang kit thật cần Chrome hoặc Edge trên máy tính (Web Serial).";
  }
  $("usb").addEventListener("click", function () {
    var r = build();
    if (!r.ok) { return; }
    status("Chọn cổng COM của kit trong hộp thoại của trình duyệt…", "");
    sendToKit(r.bytes).then(function () {
      status("Đã gửi " + r.bytes.length + " byte. Kit đang chạy chương trình và sẽ nhớ nó sau khi tắt nguồn.", "ok");
    }, function (e) {
      status(e && e.name === "NotFoundError" ? "Chưa chọn cổng nào." : "Không gửi được: " + (e && e.message ? e.message : e), "bad");
    });
  });

  /* ------------------------------------------------------------------ start */
  /* blocks.html?ex=ball opens that example; otherwise the program of last time,
   * and on a first visit the bouncing ball (something moves at once) */
  var wanted = null, saved = null;
  try { wanted = new URLSearchParams(location.search).get("ex"); } catch (e) { wanted = null; }
  wanted = examples.filter(function (x) { return x.id === wanted; })[0] || null;
  if (!wanted) {
    try { saved = JSON.parse(localStorage.getItem(STORE_KEY) || "null"); } catch (e) { saved = null; }
    if (saved) {
      try { load(saved); $("about").textContent = "Chương trình lần trước của bạn, lưu trong trình duyệt này."; }
      catch (e) { saved = null; }
    }
  }
  if (!saved && examples.length) {
    loadExample(wanted || examples.filter(function (x) { return x.id === "ball"; })[0] || examples[0]);
  }

  AkKitUI.create($("kit"), { caption: "AK Base Kit", sound: true }).then(function (k) {
    kit = k;
    run();
  }, function () {
    status("Không khởi động được kit giả lập trong trang.", "bad");
  });

  window.addEventListener("resize", function () { Blockly.svgResize(workspace); });
}());
