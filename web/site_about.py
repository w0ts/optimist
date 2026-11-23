# SPDX-License-Identifier: GPL-3.0-only
"""The site's About page (the front page, https://w0ts.github.io/optimist/): the user text, as web sections.

The user text lives here, not in README.md: the README is for developers (build, credits, licence) and points to this page.
A test (web/test_builder.mjs) checks every link of the README's Credits is on this page and the features, flavours and
install steps are named here. The page claims only what the docs it points to say; the long reference blocks are folds
(details.sec)."""

GH = "https://github.com/w0ts/optimist"
DOCS = GH + "/blob/main/docs/"
EDITOR = "webapp/editor/"
SCREENS = ("device-mixer.webp", "device-drums.webp", "device-master.webp")   # (assets/screens/: copied to the site by make_site.py)
SCREEN_ALTS = ("The FM-1's screen: the mixer with the tracks T1 to T3 and the drum track playing",
               "The FM-1's screen: the drum track's lanes with their steps",
               "The FM-1's screen: the master page with its level meter and the three effect returns")

WHAT = ("Optimist is a modular firmware for the M-VAVE FM-1: pick what goes into your FM-1, build it in the browser, try it in "
        "the browser, install it from the browser. It is free software (GPL-3.0-only), built on "
        '<a href="https://github.com/isod89/sloop-fm1">SLOOP</a>, Felucca, Melodee, X0X and other FM-1 projects (see '
        '<a href="#credits">Credits</a>).')

WARNING = ("<b>Work in progress.</b> An install writes only the firmware area, not the area where the FM-1 keeps your projects, "
           "presets, kits and samples. An install can fail, its formats change between versions, and coming from another "
           "firmware it may erase what it cannot read (SLOOP's data is kept). Back up first. No warranty. Unofficial: not made, "
           "endorsed or supported by M-VAVE.")

FEATURES = """<ul>
<li><b>Four tracks:</b> three synth, drum with 10 lanes.</li>
<li><b>Many synth engines</b>, with two LFOs and two envelopes per sound.</li>
<li><b>Drums:</b> sampled and synthesised kits, X0X 909 and 808.</li>
<li><b>Effects:</b> two insert slots on every track, three returns, punch-in effects.</li>
<li><b>Sequencer:</b> 64 steps a track, parameter locks, motion recording, chance and conditions, note generators, fills, patterns and scenes, undo.</li>
<li><b>Connections:</b>
<ul>
<li><a href="https://github.com/w0ts/jank">JANK mode</a>: jam synced over Wi-Fi / Ableton Link with no router.</li>
<li>Ableton Link, MIDI clock in and out.</li>
<li>MIDI in: USB, TRS, Bluetooth. MIDI out: USB and Bluetooth.</li>
<li>Bluetooth audio.</li>
</ul></li>
<li><b>SD card mode:</b> projects, presets, kits and samples on a microSD card, samples streamed from it (<a href="https://github.com/w0ts/optimist/blob/main/hardware/README.md">the SD board</a>).</li>
</ul>"""

FLAVOURS = """<p>Optimist is meant to be built with the web builder and customised to your needs, but there are a few stock builds to try it out.</p>
<table>
<thead><tr><th>Flavour</th><th>What it is</th></tr></thead>
<tbody>
<tr><td><b>mots-config</b> (the default)</td><td>the default build</td></tr>
<tr><td><b>synths</b></td><td>most of the synths</td></tr>
<tr><td><b>synths-2</b></td><td>what did not fit in the <b>synths</b> build</td></tr>
<tr><td><b>drum machine</b></td><td>everything about drums, some synths</td></tr>
</tbody>
</table>"""

TOOLS = ("The tabs above open them: Installer, Builder, Simulator and Editor. Use <b>Chrome or Edge</b> (they have Web MIDI) and a USB data cable, straight into the computer. "
         "The firmware files are also on the " + f'<a href="{GH}/releases">releases page</a>.')

FIRST_INSTALL = f"""<ol>
<li><b>Back up.</b> Open the <a href="{EDITOR}#backup">Editor</a>, <i>Connect</i>, then <b>Backup</b>: one <code>.optimist-backup</code> file with your projects, presets, kits and settings (tick <i>with the samples</i> for those too). Coming from another firmware, use its own backup as well.</li>
<li>Open the <a href="webapp/installer/">Installer</a>: <b>Connect</b> says what your FM-1 runs, <b>Choose</b> a release and a flavour (or your cloud build, or a file), <b>Install</b> and wait for <i>Done</i>.</li>
<li>The FM-1 restarts into Optimist. If an install is cut off, the FM-1 stays in update mode: press Install again.</li>
</ol>
<p>Your projects, presets, kits and samples live above the firmware: an install writes only the firmware area. Data that Optimist cannot read may not be kept, except SLOOP's: SLOOP 2.7 data is kept.</p>"""

GUIDE = f"""<ul>
<li><a href="{DOCS}guide/interface.md">The interface</a> and <a href="{DOCS}guide/shortcuts.md">every shortcut</a>: the controls, pages, settings and read-outs.</li>
<li><a href="{DOCS}guide/sound.md">Sound</a>: the engines, voices, modulation and presets. <a href="{DOCS}guide/drums.md">Drums</a>: kits, drum sounds, your samples and kits.</li>
<li><a href="{DOCS}guide/effects.md">Effects</a>: inserts, sends, returns, the master. <a href="{DOCS}guide/sequencer.md">Sequencer</a>: steps, locks, motion, patterns, the song.</li>
<li><a href="{DOCS}guide/midi-usb.md">MIDI, Bluetooth, USB audio, the web editor</a>: connections and the editor.</li>
<li><a href="{DOCS}guide/getting-back.md">Getting back</a>: to the official firmware, and what to do when the FM-1 does not start.</li>
<li><a href="{DOCS}BUILDER.md">The builder</a>: every builder item, its cost, the profiles.</li>
</ul>"""

BUILD_LOCAL = f"""<p>For developers, or to build without the cloud: macOS arm64 or Linux x86-64, Python 3.11+ (the compiler, pi32v2-llvm, runs natively on both; other hosts build it first: BUILDING.md).</p>
<pre><code>git clone {GH}.git &amp;&amp; cd optimist
make setup      # fetch the toolchain, the SDK files and the builder's Python packages
make builder    # the builder in the terminal: pick, see the budget, build
make web        # the same site on your machine (installer, editor, simulator)
make test       # the host tests</code></pre>
<p><a href="{GH}/blob/main/BUILDING.md">BUILDING.md</a> has the details: profiles, the emulator (<code>make emu</code>), the simulator, flashing from the command line (<code>make flash</code>), releases and the site. The toolchain and SDK files are downloaded from JieLi and checked by SHA-256; they are not in the repository.</p>"""

CREDITS = f"""<p>Optimist stands on other people's work. Each ported part is marked in its source files, keeps its authors' notice, and is listed with its licence and commit in <a href="{GH}/blob/main/LICENSING.md">LICENSING.md</a>.</p>
<ul>
<li><b><a href="https://github.com/isod89/sloop-fm1">SLOOP</a></b> by isod89 and contributors: the base of this firmware; Erick Buendia Barrientos (Erbubar23); <a href="https://github.com/renebohne/sloop-fm1">renebohne's SLOOP fork</a>.</li>
<li><b><a href="https://github.com/hugelton/Felucca">Felucca</a></b> by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the platform, engines, sequencer, editor and installer SLOOP is built on, and many later features; <a href="https://github.com/hugelton/Felucca-Engines">Felucca-Engines</a>; <a href="https://github.com/hugelton/CrispyZebra">CrispyZebra</a>.</li>
<li><b><a href="https://github.com/keremimo/melodee">Melodee</a></b> by Kerem Kilic (Ellic Studio) and contributors: FM6, CZ, the PROPHET voice, USB audio, TRS MIDI in, clock follow, MIDI expression, inserts, the rescue tool's checks, the second core's fault handling.</li>
<li><b><a href="https://github.com/isod89/clipper-fm1">Clipper</a></b> by 3dSam: the STRINGS, VPS, TERRAIN, WSEQ, VECTOR and WAVE engines.</li>
<li><b><a href="https://github.com/eldontyrellcorp/Mini-Tone">Mini-Tone</a></b> by Stephane Schmitz: the SID engine (after reSID 0.16 by Dag Lem).</li>
<li><b><a href="https://github.com/twochannels/CS-1">CS-1</a></b> by twochannels: the CS80 engine's voice design.</li>
<li><b><a href="https://github.com/charlesvestal/fm1-x0x">X0X</a></b> by Charles Vestal: the rescue tool, the ACID engine (Open303, TB-3PO), the X0X 909 / 808 kits (9W9, 8W8 by athousanddetails, ER-99 by Matthew Cieplak), the mixer glides, the second core's start.</li>
<li><b><a href="https://github.com/zakariachowdhury/flowstate-fm1">Flowstate</a></b> by Zakaria Chowdhury: the performance macros, the CPU guard. <b><a href="https://github.com/zednaked/jangada">Jangada</a></b> by zednaked: the SUPER engine, the second core's supply settings. <b><a href="https://github.com/bonszaimusic/caravel">Caravel</a></b> by bonszaimusic: the DRONE engine's coupled network. <b><a href="https://github.com/PatttF/fm-106">fm-106</a></b> by PatttF, after <a href="https://github.com/kayrockscreenprinting/ultramaster_kr106">Ultramaster KR-106</a>: the JUNO engine and chorus.</li>
<li>DaisySP, Rings, Plaits, MAME's uPD933, Airwindows, MSFA and Dexed, sc808; samples from Versilian Studios and Sonic Pi (CC0); fonts Terminus and JetBrains Mono (SIL OFL), Fukiai (MIT); <a href="https://github.com/simonjohansson/fm1-emulator">fm1-emulator</a> by Simon Johansson; <a href="https://github.com/kagaimiq/jl-uboot-tool">jl-uboot-tool</a> by kagaimiq; <a href="https://github.com/jvitkauskas/m-flasher">M-Flasher</a> and <a href="https://github.com/jvitkauskas/m-bootloader">m-bootloader</a> by Julius Vitkauskas: download-mode installs and the open bootloader (<a href="{DOCS}INSTALL-PATHS.md">docs/INSTALL-PATHS.md</a>).</li>
</ul>
<p>Unofficial: not made, endorsed or supported by M-VAVE. M-VAVE and FM-1 are trademarks of their owners. Product names (Juno-106, TB-303, TR-808, TR-909, DX7, CZ, Prophet-5, Prophet VS, Wavestation, SID) only say what an engine or kit models.</p>"""

LICENCE = f"""<p>Code: <b>GPL-3.0-only</b> (<a href="{GH}/blob/main/LICENSE">LICENSE</a>), the Bluetooth stack and the Wi-Fi code GPL-3.0-only OR MPL-2.0 (the Ableton Link port stays GPL-3.0-only); assets have their own terms (<a href="{GH}/blob/main/LICENSING.md">LICENSING.md</a>). No warranty. If you distribute a build, give its source under the same licence.</p>
<p>To add an engine, an effect, a note generator or a UI change, read <a href="{GH}/blob/main/CONTRIBUTING.md">CONTRIBUTING.md</a>: where the code lives, how a piece becomes a builder item, three worked examples in the tree, and how to open a pull request.</p>
<p>Issues and pull requests: <a href="{GH}">github.com/w0ts/optimist</a>. A useful report names the build (the flavour and release, or the cloud build's id), says whether it happened in the simulator, the emulator or on an FM-1, and how to reproduce it. Run <code>make test</code> before sending a change, and keep each ported part marked with its source and licence.</p>"""


def fold(title, body, level=2, open_=False):
    """a fold-out section of the shared style: details.sec with the heading inside the summary"""
    return f'<details class="sec"{" open" if open_ else ""}><summary><h{level}>{title}</h{level}></summary>\n{body}\n</details>\n'


def tools(items):
    """the web tools as a plain list (name and what it does; the tabs above link to them): (title, text)"""
    return "<ul>" + "".join(f"<li><b>{t}:</b> {d}</li>" for t, d in items) + "</ul>"


def about_body(badges):
    """the page's <main> content; badges: the App Store badges' HTML"""
    apps = tools((("Installer", "install a release flavour, your own build or a <code>.fwsc</code> file on your FM-1. It offers a backup first, and can put the official firmware back."),
                  ("Builder", "start from a flavour or a configuration you saved, choose the engines, drum kits, effects and features with a live flash and RAM budget, and press <i>Build</i> (no GitHub account needed). The page follows the build, then offers <i>Flash it</i> and <i>Try it in the simulator</i>."),
                  ("Simulator", "the firmware running in your browser, with the FM-1's panel and its keyboard shortcuts: try a build before you install it. It can copy your FM-1's memory in, and write it back."),
                  ("Editor", "the <b>Backup</b> of your whole FM-1 in one file, the projects and presets librarian, and the FM-1's screen mirrored. A build with live editing (<code>ED_LIVE</code>, not in the four flavours) adds every sound, drum, effect and setting as knobs, the drum grid and the samples (with CHOP).")))
    shots = "".join(f'<img src="assets/screens/{f}" alt="{a}" width="260" style="max-width:100%;height:auto" loading="lazy">' for f, a in zip(SCREENS, SCREEN_ALTS))
    mirror = ("The app: the FM-1's screen and panel on your iPhone, iPad or Mac, over USB or Bluetooth, and firmware installs.")
    return "".join((
        f'<section>\n<h2>What Optimist is</h2>\n<p class="lead">{WHAT}</p>\n<p class="msg warn">{WARNING}</p>\n</section>\n',
        '<section>\n<h2>The web tools</h2>\n<p>', TOOLS, '</p>\n', apps, '\n</section>\n',
        f'<section>\n<h2>What is in it</h2>\n{FEATURES}\n<p class="shots">{shots}</p>\n</section>\n',
        '<section>\n<h2>The flavours</h2>\n', FLAVOURS, '\n</section>\n',
        '<section>\n<h2>Your first install</h2>\n', FIRST_INSTALL, '\n</section>\n',
        f'<section>\n<h2>The app</h2>\n<p>{mirror}</p><p>{badges}</p>\n</section>\n',
        '<section>\n<h2>Reference</h2>\n',
        fold("The guide", GUIDE, 3), fold("Build it on your computer", BUILD_LOCAL, 3), '</section>\n',
        '<section>\n<h2 id="credits">Credits</h2>\n', CREDITS, '\n</section>\n',
        '<section>\n<h2>Licence and contributing</h2>\n', LICENCE, '\n</section>\n',
        '<footer class="site"><a href="', GH, '" target="_blank" rel="noopener">Source</a> · <a href="', GH, '/issues" target="_blank" rel="noopener">Issues</a>',
        ' · <a href="privacy.html">Privacy policy</a> · <a href="LICENSING.md">About and licences</a></footer>\n'))
