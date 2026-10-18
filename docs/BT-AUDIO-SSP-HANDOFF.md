# Bluetooth audio: pairing and encryption — handoff

Status 2026-10-18. The own-BR/EDR-stack route (route C, [BT-AUDIO-AIRPODS.md](BT-AUDIO-AIRPODS.md)) reaches the point
where the headphone/sink host asks for an authenticated, encrypted link and the FM-1 cannot yet provide one. The
layers below that point work; the pairing and encryption layer is **not implemented**.

This document is the project handoff: what works, exactly where the missing layer plugs in, and the public references
it must be built from. **It deliberately does not contain the cryptographic algorithms** (SAFER+ E1/E21/E22/E3/Ar,
the SSP functions f1/f2/f3/g, P-192/P-256 ECDH, the E0 keystream). Those must be written from the cited references and
libraries. Two sessions were unable to produce that code here; whoever takes this on writes it from the spec, not from
this file.

## What works (committed on `optimist`)

The baseband and link stages below pairing, all host-tested and verified against a Raspberry Pi (BlueZ) at 1 m:

| Layer | File | State |
|---|---|---|
| Inquiry (device discovery) | `firmware/src/bt/bt_inq.c`, `bt_inq_core.c` | works: finds the Pi, reads FHS/EIR |
| Page (connect as master) | `firmware/src/bt/bt_page.c` | works: Connect Complete on the Pi |
| LMP (minimal) | `firmware/src/bt/bt_lmp.c`, `.h` | works: features, version, name, host connection, setup complete, timing, clock offset, max slots, QoS, role/sniff reject; `tests/bt_lmp_test.c` |
| ACL / signalling glue | `firmware/src/bt/bt_acl.c` | works |
| L2CAP signalling | `firmware/src/bt/bt_l2cap.c`, `.h` | works: connect, config, info |
| SDP client | `firmware/src/bt/bt_l2cap.c` (`bl_pair` attr parse), `bt_acl.c` | works: finds the Pi A2DP sink (PSM 0x19, AVDTP 1.3, A2DP 1.4) |
| AVDTP initiator | `firmware/src/bt/bt_avdtp.c`, `.h` | discover/caps/SBC-set-conf/open scaffolding; **cannot complete** until the media L2CAP channel opens, which the security block prevents |
| Console/probe UI | `firmware/src/bt/bt_inq_con.c` | works (the `btinq`/`btpage`/`btscan`/… commands) |

Builder item: `BT_INQ_PROBE` (registry bit 280), config `optimist-work/btclassic/probe.config` (user-default,
USB_MODE=1, BLE=0, BT_INQ_PROBE=1). Hardware facts and the register map: [BT-CLASSIC-HW-FACTS.md](BT-CLASSIC-HW-FACTS.md).
Latest probe on the FM-1 and the flash log: `optimist-work/btclassic/FLASHES.md`, `PAUSED.md`.

## The blocker (observed on hardware)

AVDTP's media L2CAP channel connect returns **pending → 0x0003 (security block)**. The Pi host then runs:

```
Authentication Requested → Link Key Request (we answer negative)
                         → PIN Code Request  (we answer negative)
                         → Auth Complete 0x06 (failure)
```

Cause: `bt_lmp.c btl_features()` claims **no optional features** (all 8 feature bytes 0 — see `BTL_VERS 0x06`, the
`for (i = 0; i < 8; i++)` zero fill). With no Secure Simple Pairing and no encryption claimed, and no stored link key,
the controller cannot satisfy the host's security-mode-4 requirement, so every L2CAP service but SDP is refused. This
is expected: the pairing/encryption layer is simply not there yet.

## Where the missing layer plugs in (integration points)

Everything below is in the working, non-crypto code. The missing crypto is called **from** these points.

1. **Feature bits** — `bt_lmp.c btl_features()`. Set the feature bits for *Secure Simple Pairing (Controller Support)*,
   *Encryption*, and *Extended features* page 1 *Secure Simple Pairing (Host Support)*. (Bit positions: Core Spec
   Vol 2 Part C, "Link Manager Protocol feature mask definitions".) The extended-features response path already
   exists (`BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ`), currently returning "none".

2. **LMP PDU dispatch** — the `switch` in `bt_lmp.c` (around `BTL_FEATURES_REQ … BTL_SWITCH_REQ`). Add handlers for the
   pairing/encryption PDUs: IO capability request/response, the SSP numeric/nonce PDUs, `au_rand`/`sres` (the
   challenge/response), `encryption_mode_req`, `encryption_key_size_req`, `start_encryption_req`, `stop_encryption_req`.
   These handlers call the crypto layer (next) and return `BTL_ACCEPTED`/`BTL_NOT_ACCEPTED` or the next PDU.
   `btl_reply_ext` / the extended-PDU path is the template for the escaped (ext-opcode) PDUs.

3. **Crypto layer API** — `firmware/src/bt/bt_safer.h` exists as an **API header only** (in the `wt5` worktree at commit
   `53625f08`; not on `optimist`). Bring that header onto `optimist` and implement its `.c`. This is the file that holds
   the SAFER+ and SSP functions. **Not implemented; not includable in this session.**

4. **E0 link encryption enable** — the WL82 baseband does E0 in hardware: a ~42-byte register writer (the encryption key
   `cryptk` plus `linkcntl` bits 7/15), applied at `LMP_start_encryption_req`/`ACCEPTED`. See
   [BT-AUDIO-AIRPODS.md](BT-AUDIO-AIRPODS.md) §2.1 and [BT-CLASSIC-HW-FACTS.md](BT-CLASSIC-HW-FACTS.md) for the register
   fields. Master sets RX-decrypt on at `START_ENCRYPTION_REQ`, TX-encrypt on at `ACCEPTED` (see `PAUSED.md` Update 3).

5. **Link-key storage** — once pairing succeeds, the link key is stored (flash, with the settings) and offered on the
   next `Link Key Request` so re-connection skips pairing. No storage code exists yet; the `Link Key Request` negative
   reply is in the console/probe path.

6. **After encryption** — the AVDTP media channel (`bt_avdtp.c`) then opens, SBC streaming starts (codec bench and the
   SBC encoder measured in [BT-AUDIO-AIRPODS.md](BT-AUDIO-AIRPODS.md) §3). That part is ordinary audio/protocol work and
   needs no crypto.

## References to implement the crypto from

- **Secure Simple Pairing** (IO capability exchange, public-key exchange, authentication stages, f1/f2/f3/g, the link
  key function): Bluetooth Core Spec Vol 2 Part H (Security Specification), "Secure Simple Pairing".
- **SAFER+** (E1, E21, E22, E3, Ar/A'r), legacy authentication and the encryption key: Core Spec Vol 2 Part H,
  "Encryption" and "Authentication"; the Ar/A'r round structure and the LSB-first array convention are noted in
  `PAUSED.md` Update 3.
- **E0 stream cipher**: Core Spec Vol 2 Part E, "Encryption" (the keystream is generated in WL82 hardware here, so only
  the key setup and the enable registers are needed — §2.1 above).
- **ECDH P-192 / P-256**: micro-ecc (BSD-2-Clause) — one curve measured at ≈3.7 KB flash ([BT-AUDIO-AIRPODS.md](BT-AUDIO-AIRPODS.md) §2.2).
- **SHA-256** (for f1/f2/f3/g, which are HMAC-SHA-256): a public-domain SHA-256, ≈1.1 KB flash (ibid.).
- Size budget for the whole pairing+encryption layer: ≈7–9 KB flash, ≈0.5 KB RAM + ≈1 KB stack while pairing
  ([BT-AUDIO-AIRPODS.md](BT-AUDIO-AIRPODS.md) §2.2 table).

## Recommended path (from `PAUSED.md` Update 2)

SSP **Just Works** (NoInputNoOutput IO capability), which is what AirPods and most headphones expect and needs no PIN.
Legacy PIN pairing (0000) is simpler to reach a first encrypted link with a Pi that runs a PIN agent, but headphones
reject it, so it is only a stepping stone, and it still needs SAFER+.

## Test plan for the new layer (no hardware)

Mirror `tests/bt_lmp_test.c` (host-compiled, deterministic): feed the recorded Pi PDU sequence and check the replies,
using the published SSP/SAFER+ **test vectors** (Core Spec Vol 2 Part H has worked examples) so the crypto is verified
against known-answer tests before any flash. Then on the Pi: `bluetoothctl` with an agent, pair, confirm an encrypted
ACL (btmon shows Encryption Change), then open the AVDTP media channel.

## What this session did and did not do

- Did: this handoff; the working stages above are already committed and tested.
- Did not: write `bt_safer.c` or any pairing/encryption code — a safety classifier stopped it twice, and it was not
  worked around. This file does not contain that code either.
