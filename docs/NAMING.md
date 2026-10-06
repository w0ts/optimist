# Firmware name: Optimist (decided 2026-10-05, applied 2026-10-06)

Our FM-1 firmware will be called **Optimist**, after the small single-sail training dinghy. It fits the
family's boat names: Felucca (now Melodee), Jangada, SLOOP.

**Applied** on 2026-10-06 in `feat/builder` (the user chose not to wait for hardware): USB product
"Optimist (Felucca)", the loader "Optimist Update", the audio functions "Optimist Out / In", the version string
"OPTIMIST 0.1", the editor and installer pages, the manual (OPTIMIST.md; SLOOP.md and DEMARRAGE-RAPIDE-FR.md removed 2026-10-07), package names
`optimist-<profile>-<date>.fwsc` + `-ui.zip`, the identity FM-1_7XY (feat/backports-fixes). The build flags keep
`FELUCCA_*`. The boot logo (off by default) still draws SLOOP's wordmark: a new one is to do.

## What the rename will touch
- USB MIDI product name: use **"Optimist (Felucca)"**. Web editors find the device by matching
  `/felucca/i` in the port name; the emulator's port name follows the firmware's product name.
  Update our web editor's matching at the same time.
- Splash/boot logo, version string, console banner, README/manual title.
- Package names: `optimist-<version>.fwsc` + `optimist-<version>-ui.zip`.
- Keep the build flags as they are (`FELUCCA_*`), so ports from Melodee/SLOOP stay easy.
- Attribution section: "Optimist is based on SLOOP (isod89/sloop-fm1) and Melodee (keremimo/melodee,
  formerly Felucca by kurogedelic / Hügelton Instruments)", plus Melodee's FM6 (Kerem Kilic),
  Dexed/MSFA, and the existing GPL-3.0 notices.

## Where
At the integration merge: a new repo `~/GitHub/optimist`, with history carried over from `sloop-merged`
(SLOOP-plus + ANALOG 2, FM6, speed work, idle, MIDI clock, USB audio), then the rename on top.
