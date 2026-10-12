/*
 * blocks-compiler.js - turns a block program into the bytecode of the machine
 * in the firmware (src/demo/blk_vm.h).
 *
 *   AkBlocks.compile(workspace)  ->  { ok, bytes, errors, warnings, vars }
 *
 * workspace is what Blockly.serialization.workspaces.save() returns. The
 * compiler only reads that JSON: it needs neither Blockly nor a browser, so
 * the same file runs in the page and in the tests under Node
 * (tests/test_blocks.js).
 *
 * The program is the stack of blocks under "khi bắt đầu" (ak_start). Blocks
 * lying loose on the workspace are ignored.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) { module.exports = factory(); }
  else { root.AkBlocks = factory(); }
}(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  var VERSION = 1, VARS = 16, MAX_SIZE = 768, TEXT_MAX = 21;

  var OP = {
    END: 0x00, PUSH8: 0x01, PUSH16: 0x02, LOAD: 0x03, STORE: 0x04,
    ADD: 0x05, SUB: 0x06, MUL: 0x07, DIV: 0x08, MOD: 0x09, NEG: 0x0A,
    EQ: 0x0B, NE: 0x0C, LT: 0x0D, LE: 0x0E, GT: 0x0F, GE: 0x10,
    AND: 0x11, OR: 0x12, NOT: 0x13, JMP: 0x14, JZ: 0x15, WAIT: 0x16, MIN: 0x17,
    LED: 0x18, BEEP: 0x19, BTN: 0x1A, HIT: 0x1B, CLS: 0x1C, TEXT: 0x1D, NUM: 0x1E, PIXEL: 0x1F,
    LINE: 0x20, RECT: 0x21, FILL: 0x22, TEMP: 0x23, HUMI: 0x24, RAND: 0x25, TIME: 0x26,
    MAX: 0x27, ABS: 0x28, ERASE: 0x29
  };

  function crc16(bytes) {
    var crc = 0xFFFF;
    for (var i = 0; i < bytes.length; i++) {
      crc ^= bytes[i] << 8;
      for (var k = 0; k < 8; k++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
      }
    }
    return crc;
  }

  /* The font of the kit is ASCII: "Nhiệt độ" is sent as "Nhiet do". */
  function toAscii(s) {
    return String(s).normalize("NFD").replace(/[̀-ͯ]/g, "")
      .replace(/đ/g, "d").replace(/Đ/g, "D").replace(/[^\x20-\x7e]/g, "?");
  }

  function Compiler(workspace) {
    this.out = [];
    this.errors = [];
    this.warnings = [];
    this.varSlot = {};          /* variable id -> slot */
    this.varName = {};
    this.nvars = 0;
    this.temps = 0;             /* hidden counters of "repeat n times", from slot 15 down */
    var list = (workspace && workspace.variables) || [];
    for (var i = 0; i < list.length; i++) { this.varName[list[i].id] = list[i].name; }
  }

  Compiler.prototype.error = function (block, msg) {
    this.errors.push({ blockId: block ? block.id : null, message: msg });
  };

  Compiler.prototype.emit = function () {
    for (var i = 0; i < arguments.length; i++) { this.out.push(arguments[i] & 0xFF); }
  };

  Compiler.prototype.push = function (block, v) {
    v = Math.round(Number(v));
    if (!isFinite(v)) { this.error(block, "Giá trị không phải là số."); v = 0; }
    if (v < -32768 || v > 32767) {
      this.error(block, "Số " + v + " nằm ngoài khoảng −32768 … 32767.");
      v = 0;
    }
    if (v >= -128 && v <= 127) { this.emit(OP.PUSH8, v); }
    else { this.emit(OP.PUSH16, v, v >> 8); }
  };

  /* a jump whose target comes later: returns the place to patch */
  Compiler.prototype.jump = function (op) {
    this.emit(op, 0, 0);
    return this.out.length - 2;
  };

  Compiler.prototype.patch = function (at, target) {
    var d = target - (at + 2);
    this.out[at] = d & 0xFF;
    this.out[at + 1] = (d >> 8) & 0xFF;
  };

  Compiler.prototype.jumpTo = function (op, target) {
    this.patch(this.jump(op), target);
  };

  Compiler.prototype.slot = function (block, field) {
    var f = block.fields && block.fields[field];
    var id = f && (f.id || f);
    if (!id) { this.error(block, "Khối chưa chọn biến."); return 0; }
    if (!(id in this.varSlot)) {
      if (this.nvars + this.temps >= VARS) {
        this.error(block, "Quá nhiều biến (tối đa " + VARS + ", kể cả bộ đếm của các khối lặp).");
        return 0;
      }
      this.varSlot[id] = this.nvars++;
    }
    return this.varSlot[id];
  };

  Compiler.prototype.field = function (block, name, fallback) {
    var f = block.fields && block.fields[name];
    return (f === undefined || f === null) ? fallback : f;
  };

  Compiler.prototype.input = function (block, name) {
    var i = block.inputs && block.inputs[name];
    return i ? (i.block || i.shadow || null) : null;
  };

  /* a value socket: the code that leaves one number on the stack */
  Compiler.prototype.value = function (block, name) {
    var b = this.input(block, name);
    if (!b) {
      this.error(block, "Khối này còn một ô trống chưa có giá trị.");
      this.emit(OP.PUSH8, 0);
      return;
    }
    this.expr(b);
  };

  Compiler.prototype.expr = function (b) {
    var f, self = this;
    function two(op) { self.value(b, "A"); self.value(b, "B"); self.emit(op); }

    switch (b.type) {
    case "math_number":
      this.push(b, this.field(b, "NUM", 0));
      break;
    case "logic_boolean":
      this.emit(OP.PUSH8, this.field(b, "BOOL", "TRUE") === "TRUE" ? 1 : 0);
      break;
    case "variables_get":
      this.emit(OP.LOAD, this.slot(b, "VAR"));
      break;
    case "math_arithmetic":
      f = { ADD: OP.ADD, MINUS: OP.SUB, MULTIPLY: OP.MUL, DIVIDE: OP.DIV }[this.field(b, "OP", "ADD")];
      if (f === undefined) { this.error(b, "Phép luỹ thừa chưa được hỗ trợ."); f = OP.ADD; }
      two(f);
      break;
    case "math_modulo":
      this.value(b, "DIVIDEND"); this.value(b, "DIVISOR"); this.emit(OP.MOD);
      break;
    case "math_single":
      this.value(b, "NUM");
      f = { ABS: OP.ABS, NEG: OP.NEG }[this.field(b, "OP", "ABS")];
      if (f === undefined) { this.error(b, "Chỉ hỗ trợ giá trị tuyệt đối và đổi dấu."); f = OP.ABS; }
      this.emit(f);
      break;
    case "math_random_int":
      this.value(b, "FROM"); this.value(b, "TO"); this.emit(OP.RAND);
      break;
    case "ak_minmax":
      two(this.field(b, "OP", "MIN") === "MAX" ? OP.MAX : OP.MIN);
      break;
    case "logic_compare":
      two({ EQ: OP.EQ, NEQ: OP.NE, LT: OP.LT, LTE: OP.LE, GT: OP.GT, GTE: OP.GE }[this.field(b, "OP", "EQ")] || OP.EQ);
      break;
    case "logic_operation":
      two(this.field(b, "OP", "AND") === "OR" ? OP.OR : OP.AND);
      break;
    case "logic_negate":
      this.value(b, "BOOL"); this.emit(OP.NOT);
      break;
    case "ak_button":
      this.emit(OP.BTN, Number(this.field(b, "BTN", 1)));
      break;
    case "ak_pressed":
      this.emit(OP.HIT, Number(this.field(b, "BTN", 1)));
      break;
    case "ak_temp":
      this.emit(OP.TEMP);
      break;
    case "ak_humi":
      this.emit(OP.HUMI);
      break;
    case "ak_time":
      this.emit(OP.TIME, { HOUR: 0, MINUTE: 1, SECOND: 2, UPTIME: 3 }[this.field(b, "WHAT", "SECOND")] || 0);
      break;
    default:
      this.error(b, "Khối \"" + b.type + "\" không cho ra giá trị mà máy hiểu được.");
      this.emit(OP.PUSH8, 0);
    }
  };

  /* the blocks plugged into a statement socket, top to bottom */
  Compiler.prototype.body = function (block, name) {
    this.chain(this.input(block, name));
  };

  Compiler.prototype.chain = function (b) {
    while (b) {
      this.stmt(b);
      b = b.next && b.next.block;
    }
  };

  Compiler.prototype.stmt = function (b) {
    var top, out, i, n, ends, t, style;

    switch (b.type) {
    case "ak_forever":
      top = this.out.length;
      this.body(b, "DO");
      this.jumpTo(OP.JMP, top);
      break;

    case "controls_repeat_ext":
      if (this.nvars + this.temps >= VARS) {
        this.error(b, "Quá nhiều khối lặp lồng nhau và biến (tối đa " + VARS + ").");
        break;
      }
      t = VARS - 1 - this.temps++;
      this.value(b, "TIMES");
      this.emit(OP.STORE, t);
      top = this.out.length;
      this.emit(OP.LOAD, t, OP.PUSH8, 0, OP.GT);
      out = this.jump(OP.JZ);
      this.body(b, "DO");
      this.emit(OP.LOAD, t, OP.PUSH8, 1, OP.SUB, OP.STORE, t);
      this.jumpTo(OP.JMP, top);
      this.patch(out, this.out.length);
      this.temps--;
      break;

    case "controls_whileUntil":
      top = this.out.length;
      this.value(b, "BOOL");
      if (this.field(b, "MODE", "WHILE") === "UNTIL") { this.emit(OP.NOT); }
      out = this.jump(OP.JZ);
      this.body(b, "DO");
      this.jumpTo(OP.JMP, top);
      this.patch(out, this.out.length);
      break;

    case "controls_if":
      n = 1 + ((b.extraState && b.extraState.elseIfCount) || 0);
      ends = [];
      for (i = 0; i < n; i++) {
        this.value(b, "IF" + i);
        out = this.jump(OP.JZ);
        this.body(b, "DO" + i);
        if (i < n - 1 || (b.extraState && b.extraState.hasElse)) { ends.push(this.jump(OP.JMP)); }
        this.patch(out, this.out.length);
      }
      if (b.extraState && b.extraState.hasElse) { this.body(b, "ELSE"); }
      for (i = 0; i < ends.length; i++) { this.patch(ends[i], this.out.length); }
      break;

    case "ak_wait":
      this.value(b, "MS"); this.emit(OP.WAIT);
      break;

    case "variables_set":
      this.value(b, "VALUE"); this.emit(OP.STORE, this.slot(b, "VAR"));
      break;

    case "math_change":
      t = this.slot(b, "VAR");
      this.emit(OP.LOAD, t); this.value(b, "DELTA"); this.emit(OP.ADD, OP.STORE, t);
      break;

    case "ak_led":
      this.emit(OP.PUSH8, this.field(b, "STATE", "ON") === "ON" ? 1 : 0, OP.LED);
      break;

    case "ak_beep":
      this.value(b, "HZ"); this.value(b, "MS"); this.emit(OP.BEEP);
      break;

    case "ak_cls":
      this.emit(OP.CLS);
      break;

    case "ak_text":
      t = toAscii(this.field(b, "TEXT", ""));
      if (t !== String(this.field(b, "TEXT", ""))) {
        this.warnings.push({ blockId: b.id, message: "Màn hình của kit chỉ có chữ không dấu: sẽ hiện \"" + t + "\"." });
      }
      if (t.length > TEXT_MAX) {
        this.warnings.push({ blockId: b.id, message: "Chữ dài quá " + TEXT_MAX + " ký tự, phần sau bị cắt." });
        t = t.slice(0, TEXT_MAX);
      }
      this.value(b, "X"); this.value(b, "Y");
      this.emit(OP.PUSH8, Number(this.field(b, "SIZE", 1)), OP.TEXT, t.length);
      for (i = 0; i < t.length; i++) { this.emit(t.charCodeAt(i)); }
      break;

    case "ak_number":
      this.value(b, "VALUE"); this.value(b, "X"); this.value(b, "Y");
      this.emit(OP.PUSH8, Number(this.field(b, "SIZE", 1)), OP.NUM);
      break;

    case "ak_pixel":
      this.value(b, "X"); this.value(b, "Y");
      this.emit(OP.PUSH8, this.field(b, "STATE", "ON") === "ON" ? 1 : 0, OP.PIXEL);
      break;

    case "ak_line":
      this.value(b, "X0"); this.value(b, "Y0"); this.value(b, "X1"); this.value(b, "Y1"); this.emit(OP.LINE);
      break;

    case "ak_rect":
      style = this.field(b, "STYLE", "OUTLINE");
      this.value(b, "X"); this.value(b, "Y"); this.value(b, "W"); this.value(b, "H");
      this.emit(style === "FILLED" ? OP.FILL : style === "ERASE" ? OP.ERASE : OP.RECT);
      break;

    default:
      this.error(b, "Khối \"" + b.type + "\" chưa được hỗ trợ.");
    }
  };

  function compile(workspace) {
    var c = new Compiler(workspace);
    var tops = (workspace && workspace.blocks && workspace.blocks.blocks) || [];
    var starts = tops.filter(function (b) { return b.type === "ak_start"; });
    var bytes, i;

    if (starts.length === 0) {
      c.error(null, "Chưa có khối \"khi bắt đầu\". Kéo nó từ nhóm Điều khiển ra và nối các khối khác vào dưới.");
    }
    else {
      if (starts.length > 1) {
        c.error(starts[1], "Chỉ được có một khối \"khi bắt đầu\".");
      }
      c.chain(starts[0].next && starts[0].next.block);
    }
    c.emit(OP.END);

    bytes = new Uint8Array(4 + c.out.length);
    bytes[0] = 0x42; bytes[1] = 0x4B; bytes[2] = VERSION; bytes[3] = VARS;      /* "BK" */
    for (i = 0; i < c.out.length; i++) { bytes[4 + i] = c.out[i]; }
    if (bytes.length > MAX_SIZE) {
      c.error(null, "Chương trình dài " + bytes.length + " byte, kit chỉ chứa được " + MAX_SIZE + " byte.");
    }
    return {
      ok: c.errors.length === 0,
      bytes: bytes,
      errors: c.errors,
      warnings: c.warnings,
      vars: Object.keys(c.varSlot).map(function (id) { return { name: c.varName[id] || id, slot: c.varSlot[id] }; })
    };
  }

  return { compile: compile, crc16: crc16, toAscii: toAscii, OP: OP, VERSION: VERSION, MAX_SIZE: MAX_SIZE };
}));
