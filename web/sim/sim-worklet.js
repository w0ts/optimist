// SPDX-License-Identifier: GPL-3.0-only
// The simulator's audio thread (docs/SIMULATOR.md): one instance of a build's firmware (firmware/sim/sim.c, compiled
// by tools/sim.py --wasm) runs here, inside the AudioWorklet, so the sound never waits for the page. The page
// (web/sim/sim.js) sends the keys, buttons, knobs and MIDI as messages; this thread sends back the screen (the bands
// that changed), the LEDs, the firmware's MIDI out, the flash when it changed (for the browser's saved state) and the
// status (a reboot asked: the page starts a fresh instance on the same flash).
//
// The firmware renders at 44.1 kHz in blocks of its own (sim_run advances simulated time, sim_audio_take says how many
// frames it made); a FIFO sits between those blocks and the 128-frame quanta, with a linear resampler when the
// context does not run at 44.1 kHz.
const RATE = 44100;
const FRAME_EVERY = 1 / 30;          // the screen: 30 updates a second at most
const FLASH_EVERY = 2;               // the flash: saved at most every 2 s (seconds of audio time)
const FIFO = 16384;                  // stereo frames

class OptimistSim extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions || {};
    this.ok = false;
    this.port.onmessage = (e) => this.command(e.data);
    try {
      const inst = new WebAssembly.Instance(new WebAssembly.Module(o.wasm), {});
      this.c = inst.exports;
      if (this.c._initialize) this.c._initialize();
      if (o.flash && o.flash.byteLength === this.c.sim_flash_size()) this.u8(this.c.sim_flash(), o.flash.byteLength).set(new Uint8Array(o.flash));
      this.c.sim_boot();
    } catch (e) {
      this.port.postMessage({ type: "error", message: String(e && e.message || e) });
      return;
    }
    const c = this.c;
    this.ok = true;
    this.fifo = new Float32Array(FIFO * 2);
    this.rd = 0; this.wr = 0; this.n = 0;
    this.step = RATE / sampleRate;  // input frames per output frame
    this.pos = 0;                   // the fractional read position (resampling)
    this.frameT = 0; this.flashT = 0; this.t = 0;
    this.lastRev = -1; this.lastFlash = c.sim_flash_rev();
    this.stopped = false;
    this.midiQ = [];
    const names = this.text(c.sim_button_names()), enc = c.sim_enc_names ? this.text(c.sim_enc_names()) : "";
    this.port.postMessage({ type: "ready", info: { keys: c.sim_info(0), buttons: c.sim_info(1), encoders: c.sim_info(2), rate: c.sim_info(4),
      leds: c.sim_led_count(), buttonNames: names ? names.split("\n").filter(Boolean) : [], encoderNames: enc ? enc.split("\n").filter(Boolean) : [] } });
    this.check(c.sim_status());
  }
  u8(ptr, len) { return new Uint8Array(this.c.memory.buffer, ptr, len); }
  text(ptr) {
    if (!ptr) return "";
    const m = new Uint8Array(this.c.memory.buffer);
    let end = ptr;
    while (m[end]) end++;
    let s = "";
    for (let i = ptr; i < end; i++) s += String.fromCharCode(m[i]);
    return s;
  }
  command(m) {
    if (!this.ok || this.stopped) return;
    const c = this.c;
    switch (m.type) {
      case "key": c.sim_key(m.i >>> 0, m.down ? 1 : 0); break;
      case "button": c.sim_button(m.i >>> 0, m.down ? 1 : 0); break;
      case "enc": c.sim_enc(m.i >>> 0, m.delta | 0); break;
      case "master": c.sim_master(Math.max(0, Math.min(1023, m.value | 0))); break;
      case "midi":     // (queued: fed one message per block, see feedMidi)
        this.midiQ.push(m.data instanceof Uint8Array ? m.data : new Uint8Array(m.data));
        break;
      case "flash": this.sendFlash(); break;   // the page asks (it is closing, or a backup of the state)
      case "frame": this.lastRev = -1; this.sendFrame(); break;   // the whole screen again (the tab shown)
    }
  }
  check(status) {
    if (!status || this.stopped) return;
    this.stopped = true;   // a reboot, an update or a fault: this instance is done; the page starts the next one
    this.sendFlash();
    this.port.postMessage({ type: "status", status });
  }
  sendFlash() {
    const c = this.c;
    const copy = this.u8(c.sim_flash(), c.sim_flash_size()).slice();
    this.lastFlash = c.sim_flash_rev();
    this.port.postMessage({ type: "flash", flash: copy.buffer }, [copy.buffer]);
  }
  sendFrame() {
    const c = this.c, rev = c.sim_frame_rev();
    const leds = this.u8(c.sim_leds(), c.sim_led_count()).slice();
    if (rev === this.lastRev) { this.port.postMessage({ type: "leds", leds }, [leds.buffer]); return; }
    const all = this.lastRev === -1, mask = c.sim_dirty() | (all ? 0x7fff : 0);
    this.lastRev = rev;
    const px = new Uint16Array(c.memory.buffer, c.sim_pixels(), 240 * 240);
    const bands = [];
    for (let b = 0; b < 15; b++) if (mask & (1 << b)) bands.push([b, px.slice(b * 16 * 240, (b + 1) * 16 * 240)]);
    this.port.postMessage({ type: "frame", bands, leds, midiLost: c.sim_info(6) }, [leds.buffer, ...bands.map(([, a]) => a.buffer)]);
  }
  /* the next queued MIDI message into the firmware. One per block (256 frames, 5.8 ms of its time): the FM-1's USB holds
     the host back until the firmware took a message (the endpoint NAKs); here nothing would, and a second SysEx
     arriving before the main loop served the first would overwrite it in the one-frame collector (usb.c) */
  feedMidi() {
    const d = this.midiQ.shift();
    if (!d) return;
    const c = this.c;
    for (let at = 0; at < d.length; at += 8192) {
      const part = d.subarray(at, at + 8192);
      this.u8(c.sim_midi_buf(), part.length).set(part);
      c.sim_midi_in(part.length);
    }
  }
  // run the firmware until the FIFO holds `need` frames (or it stops)
  fill(need) {
    const c = this.c;
    let guard = 0;
    while (this.n < need && !this.stopped && guard++ < 64) {
      let status;
      try { this.feedMidi(); status = c.sim_run(256); }
      catch (e) {   /* a trap in the firmware (sim.c cannot catch those): a fault, as the FM-1's crash screen */
        this.port.postMessage({ type: "error", message: `the firmware stopped: ${e && e.message || e}`, fault: true });
        this.check(4);
        return;
      }
      this.check(status);
      const got = c.sim_audio_take();
      if (got) {
        const src = new Int16Array(c.memory.buffer, c.sim_audio(), got * 2);
        for (let i = 0; i < got && this.n < FIFO; i++) {
          this.fifo[this.wr * 2] = src[i * 2] / 32768;
          this.fifo[this.wr * 2 + 1] = src[i * 2 + 1] / 32768;
          this.wr = (this.wr + 1) % FIFO; this.n++;
        }
      }
      const out = c.sim_midi_out();
      if (out) this.port.postMessage({ type: "midi", data: this.u8(c.sim_midi_out_buf(), out).slice() });
    }
  }
  process(inputs, outputs) {
    if (!this.ok) return true;
    const out = outputs[0], L = out[0], R = out[1] || out[0], len = L.length;
    this.fill(Math.ceil(len * this.step) + 2);
    for (let i = 0; i < len; i++) {
      if (this.n < 2) { L[i] = R[i] = 0; continue; }       // (stopped, or a slow host: silence, not noise)
      const a = this.rd, b = (this.rd + 1) % FIFO, f = this.pos;
      L[i] = this.fifo[a * 2] + (this.fifo[b * 2] - this.fifo[a * 2]) * f;
      if (out[1]) R[i] = this.fifo[a * 2 + 1] + (this.fifo[b * 2 + 1] - this.fifo[a * 2 + 1]) * f;
      this.pos += this.step;
      while (this.pos >= 1 && this.n > 1) { this.pos -= 1; this.rd = (this.rd + 1) % FIFO; this.n--; }
    }
    this.t += len / sampleRate;
    if (!this.stopped && this.t - this.frameT >= FRAME_EVERY) { this.frameT = this.t; this.sendFrame(); }
    if (!this.stopped && this.t - this.flashT >= FLASH_EVERY) {
      this.flashT = this.t;
      if (this.c.sim_flash_rev() !== this.lastFlash) this.sendFlash();
    }
    return true;
  }
}
registerProcessor("optimist-sim", OptimistSim);
