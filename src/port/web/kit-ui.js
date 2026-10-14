/*
 * kit-ui.js - one AK Base Kit on a web page: display, three buttons, buzzer,
 * around one instance of the firmware (ak-kit.js, WebAssembly).
 *
 *   AkKitUI.create(element, options).then(function (kit) { ... })
 *
 * options: keys      {"1": 1, "2": 2, "3": 4}  keyboard key -> button bit (default: 1 2 3)
 *          caption   text in the top line of the kit
 *          sound     false = no sound switch on this kit
 * kit:     open(name)            go to a screen ("tetris", "3d", "menu", ...)
 *          autoplay(on)          games play themselves
 *          tour(list) / stopTour()   list of [name, milliseconds]; stops when a button is pressed
 *          send(line)            a shell command to the console of the kit
 *          onConsole(text)       what the kit prints
 *          onRs485(bytes) / rs485In(bytes)   the RS485 wires
 *          runBlocks(bytes)      run a block program (see blocks.html)
 *          loadClip(bytes), testFatal(), module (the WebAssembly instance)
 *          pause(on)             stop / continue the firmware's clock
 *          step()                paused: run until a task got a message; returns the milliseconds that passed
 *          trace()               the messages handed to tasks since the last call: [{t, des, src, sig, exe}]
 *          names()               {NAME: number} of the task ids and signals in a trace
 *
 * A page may hold several kits; each one is its own firmware.
 */
var AkKitUI = (function () {
  "use strict";
  var W = 128, H = 64, TICK_MS = 10, MIN_PRESS_MS = 70, VOLUME = 0.2;
  var audio = null;                     /* one audio context for the page */
  var styled = false;

  var CSS =
    ".akkit{background:var(--akkit-surface,#111821);border:1px solid var(--akkit-line,#223041);border-radius:10px;padding:16px;" +
    "color:var(--akkit-text,#d9e1ea);font:14px/1.5 ui-monospace,'Cascadia Mono',Consolas,Menlo,monospace}" +
    ".akkit-top{display:flex;align-items:center;gap:10px;font-size:13px;color:var(--akkit-muted,#8d9bab);margin-bottom:10px}" +
    ".akkit-led{width:9px;height:9px;border-radius:50%;background:#24313f;flex:none}" +
    ".akkit-led.on{background:var(--akkit-accent,#a8ff3e);box-shadow:0 0 8px var(--akkit-accent,#a8ff3e)}" +
    ".akkit-state{margin-left:auto;white-space:nowrap}" +
    ".akkit-screen{background:#000;border:1px solid var(--akkit-line,#223041);border-radius:6px;padding:12px}" +
    ".akkit canvas{display:block;width:100%;height:auto;image-rendering:pixelated;image-rendering:crisp-edges}" +
    ".akkit-buttons{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}" +
    ".akkit-buttons button{font:600 15px ui-monospace,Consolas,monospace;color:var(--akkit-text,#d9e1ea);background:var(--akkit-surface-2,#16202b);" +
    "border:1px solid var(--akkit-line,#223041);border-radius:6px;padding:12px 0;cursor:pointer;touch-action:none;user-select:none;-webkit-user-select:none}" +
    ".akkit-buttons button small{display:block;font-weight:400;font-size:12px;color:var(--akkit-muted,#8d9bab);margin-top:2px}" +
    ".akkit-buttons button.down{background:var(--akkit-accent-dim,rgba(168,255,62,.12));border-color:var(--akkit-accent,#a8ff3e);color:var(--akkit-accent,#a8ff3e)}" +
    ".akkit-row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin-top:12px;min-height:34px}" +
    ".akkit-row button{font:500 14px ui-monospace,Consolas,monospace;color:var(--akkit-muted,#8d9bab);background:transparent;" +
    "border:1px solid var(--akkit-line,#223041);border-radius:6px;padding:6px 11px;cursor:pointer}" +
    ".akkit-row button[aria-pressed=true]{color:var(--akkit-accent,#a8ff3e);border-color:var(--akkit-accent,#a8ff3e);background:var(--akkit-accent-dim,rgba(168,255,62,.12))}" +
    ".akkit-tour{color:var(--akkit-accent,#a8ff3e);font-size:13px}" +
    ".akkit button:focus-visible{outline:2px solid var(--akkit-accent,#a8ff3e);outline-offset:2px}" +
    ".akkit-error{color:#ff6b6b}";

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) { e.className = cls; }
    if (text) { e.textContent = text; }
    return e;
  }

  function create(host, options) {
    options = options || {};
    var keys = options.keys || { "1": 1, "2": 2, "3": 4 };
    var keyName = {};
    Object.keys(keys).forEach(function (k) { keyName[keys[k]] = k; });

    if (!styled) {
      var st = document.createElement("style");
      st.textContent = CSS;
      document.head.appendChild(st);
      styled = true;
    }

    /* ---- DOM ---- */
    var root = el("div", "akkit");
    var top = el("div", "akkit-top");
    var led = el("span", "akkit-led");
    var state = el("span", "akkit-state", "đang nạp…");
    top.appendChild(led);
    top.appendChild(el("span", "", options.caption || "STM32L151 · OLED 128×64 · 3 nút · còi"));
    top.appendChild(state);
    var screen = el("div", "akkit-screen");
    var canvas = document.createElement("canvas");
    canvas.width = W;
    canvas.height = H;
    canvas.setAttribute("aria-label", "Màn hình của kit");
    screen.appendChild(canvas);
    var buttons = el("div", "akkit-buttons");
    var buttonOf = {};
    [[1, "B1"], [2, "B2"], [4, "B3"]].forEach(function (b) {
      var btn = el("button");
      btn.type = "button";
      btn.textContent = b[1];
      btn.appendChild(el("small", "", keyName[b[0]] ? "phím " + keyName[b[0]] : " "));
      buttons.appendChild(btn);
      buttonOf[b[0]] = btn;
    });
    var row = el("div", "akkit-row");
    var soundBtn = null;
    if (options.sound !== false) {
      soundBtn = el("button", "", "Âm thanh: tắt");
      soundBtn.type = "button";
      soundBtn.setAttribute("aria-pressed", "false");
      soundBtn.title = "Bật để nghe còi của kit; khi bật sẽ có một tiếng bíp thử";
      row.appendChild(soundBtn);
    }
    var tourNote = el("span", "akkit-tour");
    row.appendChild(tourNote);
    root.appendChild(top);
    root.appendChild(screen);
    root.appendChild(buttons);
    root.appendChild(row);
    host.appendChild(root);

    var ctx = canvas.getContext("2d");
    var image = ctx.createImageData(W, H);
    var m = null, panel = 0, io = 0, timer = 0;
    var mask = 0, last = 0, paused = false, lastPanel = new Uint8Array(W * 8), downAt = {};
    var osc = null, gain = null, soundOn = false, lastHz = -1, testUntil = 0;
    var pendingRx = [];
    var tourList = null, tourAt = 0, tourNext = 0;

    var kit = {
      module: null,
      element: root,
      onConsole: null,
      onRs485: null,
      open: function (name) {
        var bytes = new TextEncoder().encode(name);
        m.HEAPU8.set(bytes.subarray(0, 60), io);
        m.HEAPU8[io + Math.min(bytes.length, 60)] = 0;
        return !!m._web_open();
      },
      autoplay: function (on) { m._web_autoplay(on ? 1 : 0); },
      send: function (line) {
        var bytes = new TextEncoder().encode(line + "\r");
        m.HEAPU8.set(bytes.subarray(0, 4096), io);
        m._web_console_in(Math.min(bytes.length, 4096));
      },
      rs485In: function (bytes) { pendingRx.push(bytes); },
      /* a block program (bytecode from blocks-compiler.js): load it and open the
       * Blocks screen. Returns 0, or the error code of the firmware. */
      runBlocks: function (bytes) {
        m.HEAPU8.set(bytes.subarray(0, 4096), io);
        return m._web_blk_load(bytes.length);
      },
      loadClip: function (data) {
        if (data.length < 16 || String.fromCharCode(data[0], data[1], data[2], data[3]) !== "AKV1") { return "không phải file .akv"; }
        if (data.length > m._web_store_size()) { return "clip lớn hơn kho " + m._web_store_size() + " byte"; }
        m.HEAPU8.set(data, m._web_store());
        return "";
      },
      testFatal: function () { m._web_test_fatal(); },
      pause: function (on) {
        paused = !!on;
        last = performance.now();
        state.textContent = paused ? "tạm dừng" : "đang chạy";
      },
      step: function () {
        var ms;
        m._web_buttons(mask);
        ms = m._web_step();
        after();
        return ms;
      },
      trace: function () {
        var n = m._web_trace(), out = [], v = new DataView(m.HEAPU8.buffer, io, n * 8), k;
        for (k = 0; k < n; k++) {
          out.push({ t: v.getUint32(k * 8, true), des: v.getUint8(k * 8 + 4), src: v.getUint8(k * 8 + 5), sig: v.getUint8(k * 8 + 6), exe: v.getUint8(k * 8 + 7) });
        }
        return out;
      },
      names: function () {
        var n = m._web_trace_names(), out = {};
        new TextDecoder().decode(m.HEAPU8.subarray(io, io + n)).split(";").forEach(function (p) {
          var kv = p.split("=");
          if (kv.length === 2) { out[kv[0]] = +kv[1]; }
        });
        return out;
      },
      tour: function (list) {
        tourList = list;
        tourAt = -1;
        tourNext = 0;
        kit.autoplay(true);
      },
      stopTour: function () {
        tourList = null;
        tourNote.textContent = "";
      }
    };

    function fail(msg) {
      state.textContent = "lỗi";
      state.className = "akkit-state akkit-error";
      if (kit.onConsole) { kit.onConsole("\n" + msg + "\n"); }
    }

    function draw() {
      var mem = m.HEAPU8, changed = false, i;
      for (i = 0; i < W * 8; i++) {
        if (mem[panel + i] !== lastPanel[i]) { changed = true; break; }
      }
      if (!changed) { return; }
      lastPanel.set(mem.subarray(panel, panel + W * 8));
      for (var y = 0; y < H; y++) {
        var r = (y >> 3) * W, bit = 1 << (y & 7);
        for (var x = 0; x < W; x++) {
          var on = lastPanel[r + x] & bit, p = (y * W + x) * 4;
          image.data[p] = on ? 232 : 0;
          image.data[p + 1] = on ? 245 : 0;
          image.data[p + 2] = on ? 245 : 0;
          image.data[p + 3] = 255;
        }
      }
      ctx.putImageData(image, 0, 0);
    }

    function buzzer() {
      var hz = m._web_buzzer();
      if (performance.now() < testUntil) { return; }      /* the test beep is sounding */
      if (hz === lastHz || !soundOn || !audio) { return; }
      lastHz = hz;
      if (hz >= 20) {
        osc.frequency.setValueAtTime(hz, audio.currentTime);
        gain.gain.setValueAtTime(VOLUME, audio.currentTime);
      } else {
        gain.gain.setValueAtTime(0, audio.currentTime);
      }
    }

    function tourStep(now) {
      if (!tourList || now < tourNext) { return; }
      tourAt = (tourAt + 1) % tourList.length;
      tourNext = now + tourList[tourAt][1];
      kit.autoplay(true);
      kit.open(tourList[tourAt][0]);
      tourNote.textContent = "Đang tự trình diễn · bấm nút bất kỳ để tự điều khiển";
    }

    function tick() {
      var now = performance.now();
      var dt = Math.min(200, Math.max(0, Math.round(now - last)));
      if (dt < 1) { return; }
      if (paused) { last = now; return; }
      last += dt;
      if (now - last > 400) { last = now; }       /* the tab slept: do not replay the gap */
      while (pendingRx.length) {
        var chunk = pendingRx.shift();
        m.HEAPU8.set(chunk.subarray(0, 4096), io);
        m._web_rs485_in(Math.min(chunk.length, 4096));
      }
      tourStep(now);
      m._web_buttons(mask);
      try {
        m._web_tick(dt);
      } catch (err) {
        clearInterval(timer);
        fail("Firmware dừng vì lỗi WebAssembly: " + err + ". Tải lại trang để chạy lại.");
        return;
      }
      after();
    }

    /* what the firmware left for the page after it ran */
    function after() {
      var n = m._web_console_out();
      if (n && kit.onConsole) { kit.onConsole(new TextDecoder().decode(m.HEAPU8.subarray(io, io + n)).replace(/\r/g, "")); }
      n = m._web_rs485_out();
      if (n && kit.onRs485) { kit.onRs485(m.HEAPU8.slice(io, io + n)); }
      draw();
      buzzer();
      led.className = m._web_led() ? "akkit-led on" : "akkit-led";
    }

    /* ---- buttons: pointer and keyboard both just set the mask the firmware polls ---- */
    function press(bit, down, late) {
      if (down && soundOn && audio && audio.state !== "running") { audio.resume(); }
      /* the firmware debounces its buttons: a tap shorter than that would be lost */
      if (down) {
        downAt[bit] = performance.now();
        if (tourList) { kit.stopTour(); }         /* the visitor takes over */
      } else if (!late && performance.now() - (downAt[bit] || 0) < MIN_PRESS_MS) {
        setTimeout(function () { press(bit, false, true); }, MIN_PRESS_MS);
        return;
      }
      mask = down ? (mask | bit) : (mask & ~bit);
      buttonOf[bit].classList.toggle("down", down);
    }
    [1, 2, 4].forEach(function (bit) {
      var b = buttonOf[bit];
      b.addEventListener("pointerdown", function (e) { e.preventDefault(); b.setPointerCapture(e.pointerId); press(bit, true); });
      b.addEventListener("pointerup", function () { press(bit, false); });
      b.addEventListener("pointercancel", function () { press(bit, false); });
      b.addEventListener("keydown", function (e) { if (e.key === " " || e.key === "Enter") { e.preventDefault(); if (!e.repeat) { press(bit, true); } } });
      b.addEventListener("keyup", function (e) { if (e.key === " " || e.key === "Enter") { press(bit, false); } });
    });
    document.addEventListener("keydown", function (e) {
      var t = e.target && e.target.tagName;
      if (t === "INPUT" || t === "TEXTAREA" || e.repeat || e.ctrlKey || e.metaKey || e.altKey) { return; }
      if (keys[e.key]) { press(keys[e.key], true); e.preventDefault(); }
    });
    document.addEventListener("keyup", function (e) {
      if (keys[e.key]) { press(keys[e.key], false); }
    });
    window.addEventListener("blur", function () { press(1, false, true); press(2, false, true); press(4, false, true); });

    if (soundBtn) {
      soundBtn.addEventListener("click", function () {
        var AC = window.AudioContext || window.webkitAudioContext;
        soundOn = !soundOn;
        if (soundOn && !AC) { soundOn = false; return; }
        if (soundOn && !audio) { audio = new AC(); }
        if (soundOn && !osc) {
          osc = audio.createOscillator();
          gain = audio.createGain();
          osc.type = "square";
          gain.gain.value = 0;
          osc.connect(gain);
          gain.connect(audio.destination);
          osc.start();
        }
        if (osc) {
          if (soundOn) {
            /* a short beep right away: the listener knows at once whether sound gets through */
            audio.resume();
            osc.frequency.setValueAtTime(1760, audio.currentTime);
            gain.gain.setValueAtTime(VOLUME, audio.currentTime);
            gain.gain.setValueAtTime(0, audio.currentTime + 0.15);
            testUntil = performance.now() + 200;
          } else {
            gain.gain.cancelScheduledValues(audio.currentTime);
            gain.gain.setValueAtTime(0, audio.currentTime);
          }
        }
        lastHz = -1;
        soundBtn.setAttribute("aria-pressed", soundOn ? "true" : "false");
        soundBtn.textContent = "Âm thanh: " + (soundOn ? "bật" : "tắt");
      });
    }

    if (typeof AkKit !== "function") {
      fail("Không nạp được ak-kit.js (file phải nằm cạnh trang này).");
      return Promise.reject(new Error("ak-kit.js missing"));
    }
    return AkKit().then(function (module) {
      m = kit.module = module;
      panel = m._web_panel();
      io = m._web_io();
      last = performance.now();
      m._web_tick(1);                   /* the firmware starts: its screens can be opened from now on */
      state.textContent = "đang chạy";
      timer = setInterval(tick, TICK_MS);
      return kit;
    }, function (err) {
      fail("Không khởi động được WebAssembly: " + err);
      throw err;
    });
  }

  return { create: create };
}());
