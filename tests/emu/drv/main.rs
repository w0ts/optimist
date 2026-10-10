// SPDX-License-Identifier: GPL-3.0-only
// emu-drv: a line-driven FM-1 emulator session for the emulator smoke checks (tests/emu/, docs: BUILDING.md, Tests).
// Built by tests/emu/session.py against the emulator's library (fm1_emu, the fork's feat/upstream-merge), as
// play_check is, but kept running: a check reads the firmware's state between its steps and decides what to do next.
//
//   emu-drv [--state DIR/ [--fresh]] FIRMWARE.fwsc [ELF]
//
// One command a line on stdin; every reply ends with a line "ok" or "err MESSAGE". While a log is on, a "run" also
// prints one "H ..." line per audio DMA half (below).
//   run S                 S seconds of guest time
//   align                 to the next audio DMA half (a key pressed right after lands on the same block at any cost)
//   now                   the guest time (s)
//   hold IDS | release [IDS]   matrix key ids (0..13 buttons, 14..40 the note keys), comma separated; release: all
//   turn KNOB N           N detents at once; settle: until the encoders delivered them; click KNOB N: one at a time, settled
//   master V              the MASTER potentiometer (0..1023)
//   sym NAME              "ADDRESS SIZE" of an ELF symbol
//   peek A N | peekb A N | peekh A N   N words / bytes / half words at A (SYMBOL, SYMBOL+OFFSET or 0xADDRESS)
//   poke A V              one word
//   nor OFF N             N bytes of the serial NOR as the chip holds them (hex), OFF hex or decimal
//   nordump PATH          the whole NOR (1 MiB) to PATH
//   png PATH | lit        the screen as a PNG / the number of lit pixels (any channel > 40)
//   wavstart | wavstop PATH   record what the guest plays in between (24-bit stereo 44.1 kHz)
//   log on [SYM:N ...] | log off   one line a half: "H t halves=.. last_us=.. cpu_q8=.. late=.. max_us=.. SYM=w,w.."
//   flush                 save the flash state now; quit: save it and exit
use fm1_emu::{
    firmware::{elf_symbols, Symbol},
    flash_state::{Persistence, Store},
    player::{knob, Player, OSCILLATOR_HZ},
    png,
};
use std::io::{BufRead, Write};
use std::path::Path;

struct Logger {
    dbg: u32,
    extra: Vec<(String, u32, u32)>,
    last: u32,
}

struct Session {
    p: Player,
    symbols: Vec<Symbol>,
    rec: Option<Vec<[i32; 2]>>,
    log: Option<Logger>,
    pers: Option<Persistence>,
    fault: Option<String>,
}

fn number(s: &str) -> Result<u32, String> {
    match s.strip_prefix("0x") {
        Some(h) => u32::from_str_radix(h, 16).map_err(|e| format!("{s}: {e}")),
        None => s.parse().map_err(|e| format!("{s}: {e}")),
    }
}

fn arg<'a>(t: &[&'a str], i: usize) -> Result<&'a str, String> {
    t.get(i).copied().ok_or(format!("{}: missing argument {i}", t[0]))
}

impl Session {
    fn addr(&self, s: &str) -> Result<u32, String> {
        if s.starts_with("0x") {
            return number(s);
        }
        let (name, off) = s.split_once('+').unwrap_or((s, "0"));
        let sym = self.symbols.iter().find(|x| x.name == name).ok_or(format!("no symbol {name}"))?;
        Ok(sym.address + number(off)?)
    }

    fn read(&self, a: u32, size: usize) -> u32 {
        self.p.cpu.bus.read(a, size).unwrap_or(0xDEAD_BEEF)
    }

    fn words(&self, a: u32, n: u32) -> Vec<u32> {
        (0..n).map(|w| self.read(a + w * 4, 4)).collect()
    }

    fn drain_audio(&mut self) {
        match self.rec.as_mut() {
            Some(r) => r.extend(self.p.cpu.bus.audio.samples.drain(..)),
            None => self.p.cpu.bus.audio.samples.clear(),
        }
    }

    /// `secs` of guest time; with a log on, in 0.5 ms slices and one H line per audio half.
    fn advance(&mut self, secs: f64) -> Result<(), String> {
        if let Some(f) = &self.fault {
            return Err(format!("the guest faulted earlier: {f}"));
        }
        let slice: f64 = if self.log.is_some() { 0.0005 } else { 0.25 };
        let end = self.p.cpu.bus.oscillator_ticks() + (secs * OSCILLATOR_HZ) as u64;
        while self.p.cpu.bus.oscillator_ticks() < end {
            let left = (end - self.p.cpu.bus.oscillator_ticks()) as f64 / OSCILLATOR_HZ;
            if let Err(report) = self.p.run_seconds(slice.min(left).max(1.0 / OSCILLATOR_HZ)) {
                let fault = report.rsplit_once('\n').map_or(report.clone(), |(_, f)| f.to_string());
                eprintln!("{report}");
                self.fault = Some(fault.clone());
                return Err(format!("guest fault: {fault}"));
            }
            self.drain_audio();
            self.log_line();
        }
        Ok(())
    }

    fn log_line(&mut self) {
        let Some(l) = self.log.as_ref() else { return };
        let d = self.words(l.dbg, 11);
        if d[1] == l.last {
            return;
        }
        let mut line = format!(
            "H {:.4} halves={} last_us={} cpu_q8={} late={} max_us={}",
            self.p.cpu.bus.oscillator_ticks() as f64 / OSCILLATOR_HZ,
            d[1], d[8], d[9], d[5], d[2]
        );
        for (n, a, c) in &l.extra {
            let w: Vec<String> = self.words(*a, *c).iter().map(|v| v.to_string()).collect();
            line += &format!(" {n}={}", w.join(","));
        }
        println!("{line}");
        if let Some(l) = self.log.as_mut() {
            l.last = d[1];
        }
    }

    fn keys(&mut self, t: &[&str]) -> Result<(), String> {
        let down = t[0] == "hold";
        if t.len() == 1 {
            if down {
                return Err("hold: which keys".into());
            }
            self.p.held = [false; 41];
            return Ok(());
        }
        for id in t[1].split(',') {
            let i: usize = id.parse().map_err(|_| format!("bad key id {id}"))?;
            *self.p.held.get_mut(i).ok_or(format!("bad key {i}"))? = down;
        }
        Ok(())
    }

    fn save_state(&mut self) -> Result<(), String> {
        let Some(pe) = self.pers.as_mut() else { return Ok(()) };
        match pe.flush(&self.p.cpu.bus) {
            Some(Ok(())) => println!("flash state saved to {}", pe.store.image.display()),
            Some(Err(e)) => return Err(format!("flash state not saved: {e}")),
            None => println!("flash state: no flash writes to save"),
        }
        Ok(())
    }

    fn command(&mut self, t: &[&str]) -> Result<(), String> {
        match t[0] {
            "run" => self.advance(arg(t, 1)?.parse().map_err(|_| "bad seconds")?)?,
            "align" => {
                let start = self.p.cpu.bus.audio.halves;
                let mut waited = 0u64;
                while self.p.cpu.bus.audio.halves == start {
                    self.p.run(64).map_err(|e| format!("guest fault: {e}"))?;
                    waited += 64;
                    if waited > 1_000_000_000 {
                        return Err("align: the audio DMA is not running".into());
                    }
                }
                self.drain_audio();
            }
            "now" => println!("{:.6}", self.p.cpu.bus.oscillator_ticks() as f64 / OSCILLATOR_HZ),
            "hold" | "release" => self.keys(t)?,
            "turn" => {
                let n: i32 = arg(t, 2)?.parse().map_err(|_| "bad detents")?;
                self.p.encoders.turn(knob(arg(t, 1)?)?, n);
            }
            "settle" => self.p.settle_encoders(1.0)?,
            "click" => {
                let id = knob(arg(t, 1)?)?;
                let n: i32 = arg(t, 2)?.parse().map_err(|_| "bad detents")?;
                for _ in 0..n.abs() {
                    self.p.encoders.turn(id, n.signum());
                    self.p.settle_encoders(1.0)?;
                    self.advance(0.25)?;
                }
            }
            "master" => {
                let v: u16 = arg(t, 1)?.parse().map_err(|_| "bad value")?;
                self.p.cpu.bus.devices.adc.master = v.min(1023);
            }
            "sym" => {
                let name = arg(t, 1)?;
                let s = self.symbols.iter().find(|x| x.name == name).ok_or(format!("no symbol {name}"))?;
                println!("{} {}", s.address, s.size);
            }
            "peek" | "peekb" | "peekh" => {
                let a = self.addr(arg(t, 1)?)?;
                let n = number(arg(t, 2)?)?;
                let size = match t[0] { "peek" => 4, "peekh" => 2, _ => 1 };
                let v: Vec<String> = (0..n).map(|i| self.read(a + i * size as u32, size).to_string()).collect();
                println!("{}", v.join(" "));
            }
            "poke" => {
                let a = self.addr(arg(t, 1)?)?;
                let v = number(arg(t, 2)?)?;
                self.p.cpu.bus.write(a, v, 4).map_err(|e| format!("{e:?}"))?;
            }
            "nor" => {
                let off = number(arg(t, 1)?)? as usize;
                let n = number(arg(t, 2)?)? as usize;
                let b = self.p.cpu.bus.nor_bytes();
                let end = (off + n).min(b.len());
                let hex: String = b[off.min(end)..end].iter().map(|x| format!("{x:02x}")).collect();
                println!("{hex}");
            }
            "nordump" => std::fs::write(arg(t, 1)?, self.p.cpu.bus.nor_bytes()).map_err(|e| e.to_string())?,
            "png" => {
                let b = png::encode_rgb(240, 240, &self.p.cpu.bus.lcd.pixels)?;
                std::fs::write(arg(t, 1)?, b).map_err(|e| e.to_string())?;
            }
            "lit" => {
                let lit = self.p.cpu.bus.lcd.pixels.iter()
                    .filter(|&&px| [px & 255, (px >> 8) & 255, (px >> 16) & 255].iter().any(|&c| c > 40))
                    .count();
                println!("{lit}");
            }
            "wavstart" => {
                self.p.cpu.bus.audio.samples.clear();
                self.rec = Some(Vec::new());
            }
            "wavstop" => {
                let r = self.rec.take().unwrap_or_default();
                write_wav(&r, arg(t, 1)?)?;
                println!("frames {}", r.len());
            }
            "log" => {
                if arg(t, 1)? == "off" {
                    self.log = None;
                } else {
                    let dbg = self.addr("felucca_dbg")?;
                    let mut extra = vec![];
                    for x in &t[2..] {
                        let (n, c) = x.rsplit_once(':').unwrap_or((x, "1"));
                        extra.push((n.to_string(), self.addr(n)?, number(c)?));
                    }
                    let last = self.words(dbg, 2)[1];
                    self.log = Some(Logger { dbg, extra, last });
                }
            }
            "flush" => self.save_state()?,
            "quit" => {
                self.save_state()?;
                println!("ok");
                std::process::exit(0);
            }
            other => return Err(format!("unknown command {other}")),
        }
        Ok(())
    }
}

fn write_wav(frames: &[[i32; 2]], path: &str) -> Result<(), String> {
    let data = frames.len() as u32 * 6;
    let mut o = Vec::with_capacity(44 + data as usize);
    o.extend_from_slice(b"RIFF");
    o.extend_from_slice(&(36 + data).to_le_bytes());
    o.extend_from_slice(b"WAVEfmt ");
    o.extend_from_slice(&16u32.to_le_bytes());
    for v in [1u16, 2] {
        o.extend_from_slice(&v.to_le_bytes());
    }
    for v in [44_100u32, 44_100 * 6] {
        o.extend_from_slice(&v.to_le_bytes());
    }
    for v in [6u16, 24] {
        o.extend_from_slice(&v.to_le_bytes());
    }
    o.extend_from_slice(b"data");
    o.extend_from_slice(&data.to_le_bytes());
    for [l, r] in frames {
        o.extend_from_slice(&l.to_le_bytes()[..3]);
        o.extend_from_slice(&r.to_le_bytes()[..3]);
    }
    std::fs::write(path, o).map_err(|e| format!("{path}: {e}"))
}

fn main() -> Result<(), String> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let (mut state, mut fresh) = (None, false);
    while let Some(a) = args.first().cloned() {
        match a.as_str() {
            "--state" if args.len() > 1 => {
                state = Some(args[1].clone());
                args.drain(..2);
            }
            "--fresh" => {
                fresh = true;
                args.remove(0);
            }
            _ => break,
        }
    }
    let fw = args.first().ok_or("usage: emu-drv [--state DIR/ [--fresh]] FIRMWARE.fwsc [ELF]")?.clone();
    let elf = args.get(1).cloned().unwrap_or_else(|| Path::new(&fw).with_extension("elf").to_string_lossy().into());
    let symbols = elf_symbols(&std::fs::read(&elf).map_err(|e| format!("{elf}: {e}"))?)?;
    let mut p = Player::boot(Path::new(&fw), 40)?;
    if let Ok(mhz) = std::env::var("FM1_CPU_MHZ") {
        let mhz: u32 = mhz.parse().map_err(|_| "bad FM1_CPU_MHZ")?;
        p.cpu.bus.set_instruction_clock(Some(mhz.max(1) * 1_000_000));
    }
    p.cpu.idle_skip = std::env::var("FM1_IDLE_SKIP").map_or(true, |v| v != "0");
    p.cpu.spin_skip = std::env::var("FM1_SPIN_SKIP").map_or(true, |v| v != "0");
    let pers = state.map(|s| {
        let store = Store::for_firmware(Some(Path::new(&s)), Path::new(&fw));
        let (pers, msg) = Persistence::attach(store, &mut p.cpu.bus, Path::new(&fw), p.code_end, fresh);
        println!("{msg}");
        pers
    });
    let mut s = Session { p, symbols, rec: None, log: None, pers, fault: None };
    println!("ready");
    std::io::stdout().flush().ok();
    for line in std::io::stdin().lock().lines() {
        let line = line.map_err(|e| e.to_string())?;
        let t: Vec<&str> = line.split_whitespace().collect();
        if t.is_empty() {
            continue;
        }
        match s.command(&t) {
            Ok(()) => println!("ok"),
            Err(e) => println!("err {}", e.replace('\n', " | ")),
        }
        std::io::stdout().flush().ok();
    }
    s.save_state()
}
