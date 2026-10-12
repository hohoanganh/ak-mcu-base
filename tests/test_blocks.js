#!/usr/bin/env node
/*
 * Tests of the block compiler (src/port/web/blocks-compiler.js) under Node,
 * and keeper of tests/blk_fixtures.h: the bytecode of every example program,
 * which tests/test_demo.c runs in the real machine.
 *
 *   node tests/test_blocks.js            check (fails if the fixtures are stale)
 *   node tests/test_blocks.js --write    regenerate tests/blk_fixtures.h
 */
"use strict";

var fs = require("fs");
var path = require("path");

var web = path.join(__dirname, "..", "src", "port", "web");
var AkBlocks = require(path.join(web, "blocks-compiler.js"));
var examples = require(path.join(web, "blocks-examples.js"));
var OP = AkBlocks.OP;

var fails = 0, checks = 0;
function check(cond, what) {
  checks++;
  if (!cond) { fails++; console.log("  FAIL " + what); }
}
function hex(bytes) { return Array.prototype.map.call(bytes, function (b) { return ("0" + b.toString(16)).slice(-2); }).join(" "); }

/* a workspace from a list of top-level statement blocks under "khi bắt đầu" */
var n = 0;
function blk(type, fields, inputs, extra) {
  var b = { type: type, id: "t" + (++n) };
  if (fields) { b.fields = fields; }
  if (inputs) { b.inputs = inputs; }
  if (extra) { b.extraState = extra; }
  return b;
}
function num(v) { return { block: blk("math_number", { NUM: v }) }; }
function ws() {
  var list = Array.prototype.slice.call(arguments);
  for (var i = 0; i < list.length - 1; i++) { list[i].next = { block: list[i + 1] }; }
  var start = blk("ak_start");
  if (list.length) { start.next = { block: list[0] }; }
  return { blocks: { blocks: [start] }, variables: [{ name: "a", id: "va" }, { name: "b", id: "vb" }] };
}
function code(r) { return hex(r.bytes.slice(4)); }
function expectCode(r, ops, what) {
  check(r.ok, what + ": compiles (" + JSON.stringify(r.errors) + ")");
  check(code(r) === hex(ops), what + "\n       got  " + code(r) + "\n       want " + hex(ops));
}

console.log("test_blocks.js");

/* ---- image ---- */
var r = AkBlocks.compile(ws());
check(r.ok && hex(r.bytes) === "42 4b 01 10 00", "empty program: header + END, got " + hex(r.bytes));
check(AkBlocks.crc16([0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39]) === 0x29B1, "CRC-16 is CCITT, start 0xFFFF");

/* ---- numbers ---- */
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(100) }))), [OP.PUSH8, 100, OP.WAIT, OP.END], "small number: one byte");
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(500) }))), [OP.PUSH16, 0xF4, 0x01, OP.WAIT, OP.END], "500: two bytes, low first");
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(-200) }))), [OP.PUSH16, 0x38, 0xFF, OP.WAIT, OP.END], "negative 16 bit");
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(-1) }))), [OP.PUSH8, 0xFF, OP.WAIT, OP.END], "-1: one byte");
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(2.6) }))), [OP.PUSH8, 3, OP.WAIT, OP.END], "2.6 is rounded");
r = AkBlocks.compile(ws(blk("ak_wait", null, { MS: num(40000) })));
check(!r.ok && r.errors[0].blockId, "40000 is refused, and the error names the block");

/* ---- statements ---- */
expectCode(AkBlocks.compile(ws(blk("ak_led", { STATE: "ON" }), blk("ak_led", { STATE: "OFF" }))),
  [OP.PUSH8, 1, OP.LED, OP.PUSH8, 0, OP.LED, OP.END], "LED on, off");
expectCode(AkBlocks.compile(ws(blk("ak_beep", null, { HZ: num(1000), MS: num(50) }))),
  [OP.PUSH16, 0xE8, 0x03, OP.PUSH8, 50, OP.BEEP, OP.END], "beep: frequency then time");
expectCode(AkBlocks.compile(ws(blk("ak_text", { TEXT: "Hi", SIZE: "2" }, { X: num(1), Y: num(2) }))),
  [OP.PUSH8, 1, OP.PUSH8, 2, OP.PUSH8, 2, OP.TEXT, 2, 0x48, 0x69, OP.END], "text: x y size, then the characters");
expectCode(AkBlocks.compile(ws(blk("ak_number", { SIZE: "1" }, { VALUE: num(7), X: num(1), Y: num(2) }))),
  [OP.PUSH8, 7, OP.PUSH8, 1, OP.PUSH8, 2, OP.PUSH8, 1, OP.NUM, OP.END], "number: value x y size");
expectCode(AkBlocks.compile(ws(blk("ak_rect", { STYLE: "ERASE" }, { X: num(1), Y: num(2), W: num(3), H: num(4) }))),
  [OP.PUSH8, 1, OP.PUSH8, 2, OP.PUSH8, 3, OP.PUSH8, 4, OP.ERASE, OP.END], "rectangle, erased");
expectCode(AkBlocks.compile(ws(blk("ak_line", null, { X0: num(1), Y0: num(2), X1: num(3), Y1: num(4) }), blk("ak_cls"))),
  [OP.PUSH8, 1, OP.PUSH8, 2, OP.PUSH8, 3, OP.PUSH8, 4, OP.LINE, OP.CLS, OP.END], "line, clear");

/* ---- variables ---- */
expectCode(AkBlocks.compile(ws(
  blk("variables_set", { VAR: { id: "vb" } }, { VALUE: num(5) }),
  blk("math_change", { VAR: { id: "va" } }, { DELTA: { block: blk("variables_get", { VAR: { id: "vb" } }) } }))),
  [OP.PUSH8, 5, OP.STORE, 0, OP.LOAD, 1, OP.LOAD, 0, OP.ADD, OP.STORE, 1, OP.END],
  "variables get their slot in the order they are first used");

/* ---- control ---- */
expectCode(AkBlocks.compile(ws(blk("ak_forever", null, { DO: { block: blk("ak_cls") } }))),
  [OP.CLS, OP.JMP, 0xFC, 0xFF, OP.END], "forever: jump back to the top (-4)");
expectCode(AkBlocks.compile(ws(blk("controls_if", null, { IF0: { block: blk("ak_button", { BTN: "2" }) }, DO0: { block: blk("ak_cls") } }))),
  [OP.BTN, 2, OP.JZ, 1, 0, OP.CLS, OP.END], "if: skip the body when false");
expectCode(AkBlocks.compile(ws(blk("controls_if", null,
  { IF0: { block: blk("ak_pressed", { BTN: "1" }) }, DO0: { block: blk("ak_led", { STATE: "ON" }) }, ELSE: { block: blk("ak_led", { STATE: "OFF" }) } },
  { hasElse: true }))),
  [OP.HIT, 1, OP.JZ, 6, 0, OP.PUSH8, 1, OP.LED, OP.JMP, 3, 0, OP.PUSH8, 0, OP.LED, OP.END], "if / else");
expectCode(AkBlocks.compile(ws(blk("controls_if", null,
  { IF0: num(1), DO0: { block: blk("ak_cls") }, IF1: num(0), DO1: { block: blk("ak_cls") }, ELSE: { block: blk("ak_cls") } },
  { elseIfCount: 1, hasElse: true }))),
  [OP.PUSH8, 1, OP.JZ, 4, 0, OP.CLS, OP.JMP, 10, 0,
   OP.PUSH8, 0, OP.JZ, 4, 0, OP.CLS, OP.JMP, 1, 0,
   OP.CLS, OP.END], "if / else if / else: every branch jumps to the same end");
expectCode(AkBlocks.compile(ws(blk("controls_repeat_ext", null, { TIMES: num(3), DO: { block: blk("ak_cls") } }))),
  [OP.PUSH8, 3, OP.STORE, 15,
   OP.LOAD, 15, OP.PUSH8, 0, OP.GT, OP.JZ, 11, 0,
   OP.CLS, OP.LOAD, 15, OP.PUSH8, 1, OP.SUB, OP.STORE, 15, OP.JMP, 0xED, 0xFF,
   OP.END], "repeat 3 times: a hidden counter in the last slot");
expectCode(AkBlocks.compile(ws(blk("controls_whileUntil", { MODE: "UNTIL" }, { BOOL: { block: blk("ak_button", { BTN: "3" }) }, DO: { block: blk("ak_cls") } }))),
  [OP.BTN, 3, OP.NOT, OP.JZ, 4, 0, OP.CLS, OP.JMP, 0xF6, 0xFF, OP.END], "repeat until");

/* nested repeats take different counters and give them back */
r = AkBlocks.compile(ws(
  blk("controls_repeat_ext", null, { TIMES: num(2), DO: { block: blk("controls_repeat_ext", null, { TIMES: num(2), DO: { block: blk("ak_cls") } }) } }),
  blk("controls_repeat_ext", null, { TIMES: num(2), DO: { block: blk("ak_cls") } })));
var c = Array.prototype.slice.call(r.bytes.slice(4));
check(r.ok && c[3] === 15 && c.indexOf(14) > 0 && c[c.length - 3 - 20] !== undefined, "nested repeat compiles");
check(c.filter(function (v, i) { return c[i - 1] === OP.STORE && v === 13; }).length === 0, "the third repeat reuses slot 15, not 13");

/* ---- expressions ---- */
function exprCode(b) { return AkBlocks.compile(ws(blk("ak_wait", null, { MS: { block: b } }))); }
expectCode(exprCode(blk("math_arithmetic", { OP: "MINUS" }, { A: num(7), B: num(5) })), [OP.PUSH8, 7, OP.PUSH8, 5, OP.SUB, OP.WAIT, OP.END], "7 - 5");
expectCode(exprCode(blk("math_arithmetic", { OP: "DIVIDE" }, { A: num(7), B: num(5) })), [OP.PUSH8, 7, OP.PUSH8, 5, OP.DIV, OP.WAIT, OP.END], "7 / 5");
expectCode(exprCode(blk("math_modulo", null, { DIVIDEND: num(7), DIVISOR: num(5) })), [OP.PUSH8, 7, OP.PUSH8, 5, OP.MOD, OP.WAIT, OP.END], "7 mod 5");
expectCode(exprCode(blk("math_random_int", null, { FROM: num(1), TO: num(6) })), [OP.PUSH8, 1, OP.PUSH8, 6, OP.RAND, OP.WAIT, OP.END], "random 1..6");
expectCode(exprCode(blk("logic_compare", { OP: "LTE" }, { A: num(1), B: num(2) })), [OP.PUSH8, 1, OP.PUSH8, 2, OP.LE, OP.WAIT, OP.END], "1 <= 2");
expectCode(exprCode(blk("logic_operation", { OP: "OR" }, { A: { block: blk("logic_boolean", { BOOL: "TRUE" }) }, B: { block: blk("logic_boolean", { BOOL: "FALSE" }) } })),
  [OP.PUSH8, 1, OP.PUSH8, 0, OP.OR, OP.WAIT, OP.END], "true or false");
expectCode(exprCode(blk("logic_negate", null, { BOOL: { block: blk("ak_button", { BTN: "1" }) } })), [OP.BTN, 1, OP.NOT, OP.WAIT, OP.END], "not button 1");
expectCode(exprCode(blk("ak_time", { WHAT: "UPTIME" })), [OP.TIME, 3, OP.WAIT, OP.END], "seconds since start");
expectCode(exprCode(blk("ak_temp")), [OP.TEMP, OP.WAIT, OP.END], "temperature");
expectCode(exprCode(blk("math_single", { OP: "NEG" }, { NUM: num(4) })), [OP.PUSH8, 4, OP.NEG, OP.WAIT, OP.END], "-4");
expectCode(exprCode(blk("ak_minmax", { OP: "MAX" }, { A: num(4), B: num(9) })), [OP.PUSH8, 4, OP.PUSH8, 9, OP.MAX, OP.WAIT, OP.END], "larger of two");
/* a shadow block counts as the value; a real block on top of it wins */
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: { shadow: blk("math_number", { NUM: 9 }) } }))), [OP.PUSH8, 9, OP.WAIT, OP.END], "shadow value");
expectCode(AkBlocks.compile(ws(blk("ak_wait", null, { MS: { shadow: blk("math_number", { NUM: 9 }), block: blk("math_number", { NUM: 4 }) } }))),
  [OP.PUSH8, 4, OP.WAIT, OP.END], "block over a shadow");

/* ---- what is refused, and what is only warned about ---- */
r = AkBlocks.compile({ blocks: { blocks: [blk("ak_cls")] } });
check(!r.ok && /khi bắt đầu/.test(r.errors[0].message), "no start block: refused, in words");
r = AkBlocks.compile({ blocks: { blocks: [blk("ak_start"), blk("ak_start")] } });
check(!r.ok, "two start blocks: refused");
r = AkBlocks.compile(ws(blk("ak_wait", null, {})));
check(!r.ok && r.errors[0].blockId, "empty socket: refused, names the block");
r = exprCode(blk("math_arithmetic", { OP: "POWER" }, { A: num(2), B: num(3) }));
check(!r.ok, "power: refused");
r = AkBlocks.compile(ws(blk("procedures_callnoreturn")));
check(!r.ok, "a block the machine does not know: refused");
r = AkBlocks.compile(ws(blk("ak_text", { TEXT: "Nhiệt độ", SIZE: "1" }, { X: num(0), Y: num(0) })));
check(r.ok && r.warnings.length === 1 && hex(r.bytes).indexOf("4e 68 69 65 74 20 64 6f") > 0, "accents are dropped with a warning: Nhiet do");
r = AkBlocks.compile(ws(blk("ak_text", { TEXT: "1234567890123456789012345", SIZE: "1" }, { X: num(0), Y: num(0) })));
check(r.ok && r.warnings.length === 1 && r.bytes[4 + 7] === 21, "long text is cut to 21 characters with a warning");
var many = [];
for (var i = 0; i < 200; i++) { many.push(blk("ak_beep", null, { HZ: num(1000 + i), MS: num(300) })); }
r = AkBlocks.compile(ws.apply(null, many));
check(!r.ok && /768/.test(r.errors[0].message), "a program over 768 bytes is refused");
var vars = [];
for (i = 0; i < 17; i++) { vars.push(blk("variables_set", { VAR: { id: "v" + i } }, { VALUE: num(1) })); }
r = AkBlocks.compile(ws.apply(null, vars));
check(!r.ok, "a 17th variable is refused");

/* ---- the examples, and the fixtures for the C test ---- */
var out = "/* Generated by tests/test_blocks.js --write from src/port/web/blocks-examples.js. Do not edit. */\n\n";
var table = "";
examples.forEach(function (ex) {
  var res = AkBlocks.compile(ex.workspace);
  check(res.ok, "example \"" + ex.id + "\" compiles: " + JSON.stringify(res.errors));
  check(res.warnings.length === 0, "example \"" + ex.id + "\" has no warnings: " + JSON.stringify(res.warnings));
  check(res.bytes.length <= AkBlocks.MAX_SIZE, "example \"" + ex.id + "\" fits");
  var lines = [];
  for (var k = 0; k < res.bytes.length; k += 16) {
    lines.push("\t" + Array.prototype.map.call(res.bytes.slice(k, k + 16), function (b) { return "0x" + ("0" + b.toString(16).toUpperCase()).slice(-2); }).join(", ") + ",");
  }
  out += "static const uint8_t blk_fx_" + ex.id + "[] = {\t/* " + res.bytes.length + " bytes */\n" + lines.join("\n") + "\n};\n\n";
  table += "\t{ \"" + ex.id + "\", blk_fx_" + ex.id + ", sizeof(blk_fx_" + ex.id + "), 0x" + ("000" + AkBlocks.crc16(res.bytes).toString(16).toUpperCase()).slice(-4) + " },\n";
});
out += "static const struct {\n\tconst char* id;\n\tconst uint8_t* img;\n\tuint16_t len;\n\tuint16_t crc;\t\t/* as the page computes it */\n} blk_fixtures[] = {\n" + table + "};\n";

var file = path.join(__dirname, "blk_fixtures.h");
if (process.argv.indexOf("--write") >= 0) {
  fs.writeFileSync(file, out);
  console.log("  wrote " + path.relative(process.cwd(), file));
}
else {
  var have = fs.existsSync(file) ? fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n") : "";
  check(have === out, "tests/blk_fixtures.h is up to date (run: node tests/test_blocks.js --write)");
}

console.log(checks + " checks, " + fails + " failed");
process.exit(fails ? 1 : 0);
