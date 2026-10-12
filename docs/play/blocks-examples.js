/*
 * blocks-examples.js - the example programs of the block editor, as Blockly
 * workspaces. Built with a few helpers instead of written out as JSON, so
 * that an example reads like the program it is.
 *
 * Used by the page (blocks.js: the "Ví dụ" menu) and by the tests
 * (tests/test_blocks.js compiles each one; tests/blk_fixtures.h is the result,
 * which the C test then runs in the real machine).
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) { module.exports = factory(); }
  else { root.AkBlocksExamples = factory(); }
}(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  var serial = 0;
  function id() { return "ex" + (++serial); }

  /* value blocks */
  function num(v) { return { type: "math_number", id: id(), fields: { NUM: v } }; }
  function val(v) { return typeof v === "number" ? { shadow: num(v) } : { block: v }; }
  function get(v) { return { type: "variables_get", id: id(), fields: { VAR: { id: v } } }; }
  function op(type, o, a, b, an, bn) {
    var ins = {};
    ins[an || "A"] = val(a);
    ins[bn || "B"] = val(b);
    return { type: type, id: id(), fields: o ? { OP: o } : {}, inputs: ins };
  }
  function cmp(o, a, b) { return op("logic_compare", o, a, b); }
  function math(o, a, b) { return op("math_arithmetic", o, a, b); }
  function or(a, b) { return op("logic_operation", "OR", a, b); }
  function button(n) { return { type: "ak_button", id: id(), fields: { BTN: String(n) } }; }
  function pressed(n) { return { type: "ak_pressed", id: id(), fields: { BTN: String(n) } }; }
  function time(what) { return { type: "ak_time", id: id(), fields: { WHAT: what } }; }
  function simple(type) { return { type: type, id: id() }; }

  /* statement blocks */
  function led(on) { return { type: "ak_led", id: id(), fields: { STATE: on ? "ON" : "OFF" } }; }
  function wait(ms) { return { type: "ak_wait", id: id(), inputs: { MS: val(ms) } }; }
  function beep(hz, ms) { return { type: "ak_beep", id: id(), inputs: { HZ: val(hz), MS: val(ms) } }; }
  function cls() { return simple("ak_cls"); }
  function text(s, x, y, size) {
    return { type: "ak_text", id: id(), fields: { TEXT: s, SIZE: String(size || 1) }, inputs: { X: val(x), Y: val(y) } };
  }
  function number(v, x, y, size) {
    return { type: "ak_number", id: id(), fields: { SIZE: String(size || 1) }, inputs: { VALUE: val(v), X: val(x), Y: val(y) } };
  }
  function rect(style, x, y, w, h) {
    return { type: "ak_rect", id: id(), fields: { STYLE: style }, inputs: { X: val(x), Y: val(y), W: val(w), H: val(h) } };
  }
  function line(x0, y0, x1, y1) {
    return { type: "ak_line", id: id(), inputs: { X0: val(x0), Y0: val(y0), X1: val(x1), Y1: val(y1) } };
  }
  function set(v, value) { return { type: "variables_set", id: id(), fields: { VAR: { id: v } }, inputs: { VALUE: val(value) } }; }
  function change(v, by) { return { type: "math_change", id: id(), fields: { VAR: { id: v } }, inputs: { DELTA: val(by) } }; }

  /* blocks one under the other */
  function seq() {
    var list = Array.prototype.slice.call(arguments).filter(Boolean);
    for (var i = 0; i < list.length - 1; i++) { list[i].next = { block: list[i + 1] }; }
    return list[0];
  }
  function forever() {
    return { type: "ak_forever", id: id(), inputs: { DO: { block: seq.apply(null, arguments) } } };
  }
  function repeat(times) {
    return {
      type: "controls_repeat_ext", id: id(),
      inputs: { TIMES: val(times), DO: { block: seq.apply(null, Array.prototype.slice.call(arguments, 1)) } }
    };
  }
  function when(cond, then, otherwise) {
    var b = { type: "controls_if", id: id(), inputs: { IF0: { block: cond }, DO0: { block: then } } };
    if (otherwise) {
      b.extraState = { hasElse: true };
      b.inputs.ELSE = { block: otherwise };
    }
    return b;
  }
  function program(vars) {
    var start = { type: "ak_start", id: id(), x: 30, y: 30 };
    var first = seq.apply(null, Array.prototype.slice.call(arguments, 1));
    if (first) { start.next = { block: first }; }
    return {
      blocks: { languageVersion: 0, blocks: [start] },
      variables: (vars || []).map(function (n) { return { name: n, id: n }; })
    };
  }

  return [
    {
      id: "blink",
      name: "Nháy LED",
      about: "Chương trình đầu tiên: bật đèn, chờ, tắt đèn, chờ, và lặp lại mãi.",
      workspace: program([],
        forever(led(1), wait(500), led(0), wait(500)))
    },
    {
      id: "button",
      name: "Nút bấm, đèn và còi",
      about: "Giữ B1 thì đèn sáng. Bấm B2 thì còi kêu một tiếng.",
      workspace: program([],
        cls(),
        text("B1: den LED", 4, 12, 1),
        text("B2: coi", 4, 28, 1),
        forever(
          when(button(1), led(1), led(0)),
          when(pressed(2), beep(1500, 120)),
          wait(20)))
    },
    {
      id: "counter",
      name: "Đếm số lần bấm",
      about: "Một biến nhớ số lần bấm B1; B2 đưa nó về 0.",
      workspace: program(["dem"],
        forever(
          when(pressed(1), seq(change("dem", 1), beep(1800, 30))),
          when(pressed(2), set("dem", 0)),
          cls(),
          text("So lan bam B1", 4, 2, 1),
          number(get("dem"), 44, 22, 4),
          text("B2: ve 0", 4, 56, 1),
          wait(50)))
    },
    {
      id: "thermo",
      name: "Nhiệt kế",
      about: "Đọc cảm biến mỗi giây và hiện lên màn hình. Trên kit thật cần mô-đun SHT45 ở cổng I2C.",
      workspace: program([],
        forever(
          cls(),
          text("Nhiet do", 0, 0, 1),
          number(simple("ak_temp"), 0, 12, 3),
          text("C", 44, 12, 2),
          text("Do am", 72, 0, 1),
          number(simple("ak_humi"), 72, 12, 3),
          text("%", 114, 12, 2),
          line(0, 44, 127, 44),
          text("cap nhat moi giay", 0, 50, 1),
          wait(1000)))
    },
    {
      id: "clock",
      name: "Đồng hồ",
      about: "Giờ, phút, giây lấy từ đồng hồ của kit.",
      workspace: program([],
        forever(
          cls(),
          number(time("HOUR"), 8, 20, 3),
          text(":", 44, 20, 3),
          number(time("MINUTE"), 58, 20, 3),
          number(time("SECOND"), 100, 34, 1),
          wait(200)))
    },
    {
      id: "ball",
      name: "Bóng nảy",
      about: "Bốn biến: vị trí và hướng đi của quả bóng. Chạm thành thì đổi hướng và kêu.",
      workspace: program(["x", "y", "dx", "dy"],
        set("x", 20), set("y", 12), set("dx", 2), set("dy", 1),
        forever(
          cls(),
          rect("OUTLINE", 0, 0, 128, 64),
          rect("FILLED", get("x"), get("y"), 6, 6),
          change("x", get("dx")),
          change("y", get("dy")),
          when(or(cmp("LTE", get("x"), 1), cmp("GTE", get("x"), 121)),
            seq(set("dx", math("MINUS", 0, get("dx"))), beep(900, 15))),
          when(or(cmp("LTE", get("y"), 1), cmp("GTE", get("y"), 57)),
            seq(set("dy", math("MINUS", 0, get("dy"))), beep(1300, 15))),
          wait(30)))
    },
    {
      id: "countdown",
      name: "Đếm ngược",
      about: "Khối lặp một số lần: đếm từ 5 về 1 rồi báo hết giờ.",
      workspace: program(["n"],
        set("n", 5),
        repeat(5,
          cls(),
          number(get("n"), 52, 12, 4),
          beep(1000, 60),
          wait(1000),
          change("n", -1)),
        cls(),
        text("HET GIO!", 16, 22, 2),
        beep(2000, 800))
    }
  ];
}));
