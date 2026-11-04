# SPDX-License-Identifier: GPL-3.0-only
# Shortcuts for tools/optimist.py (which works without make: BUILDING.md). `make help` lists them.
.DEFAULT_GOAL := help
.PHONY: help setup sdk builder build classic package sd-zip flash profiles publish unpublish share delete web site emu emu-list emu-update test costs costs-all gate emu-check mirror

PY ?= python3
PROFILE ?= mots-config
FW ?=
CLASSIC ?=
CL = $(if $(CLASSIC),--classic)
CPU ?=
IMAGES ?= firmwares

help:
	@echo "make setup                     check and fetch what the build and the emulator need"
	@echo "make sdk                       fetch the AC79 SDK files into ./sdk when missing or stale (build, builder, package, test run it first)"
	@echo "The firmware is the release: Optimist as an fm1-base app (the submodule fm1base/: git submodule update --init"
	@echo "--recursive; docs/BUILDING.md, The release build). CLASSIC=1 on any command (or make classic): Optimist's own firmware."
	@echo "make builder                   the firmware builder menu (pick features, build: build/optimist-<version>-*.fwsc)"
	@echo "make build   [PROFILE=name]    build a profile without the menu ($(PROFILE))"
	@echo "make classic [PROFILE=name]    the same, Optimist's own firmware (main's): what make build did before the release"
	@echo "make package [PROFILE=name]    build a profile and copy .fwsc (+ -ui.zip, classic; + optimist-sd-card.zip, SDCARD) into $(IMAGES)/"
	@echo "make sd-zip                    the SD card zip alone (every factory preset and sample set): build/optimist-sd-card.zip"
	@echo "                              (every build with SDCARD makes it beside its .fwsc; cached by its inputs)"
	@echo "make flash   [PKG=file.fwsc] [KEEP_BOOTLOADER=1] [FM1=NAME|all]   build $(PROFILE) and install it (or PKG as it is); shows the identities, the route and the bootloader, and asks (several FM-1s: which one, or all, one after the other)"
	@echo "make profiles                  the profiles: published (CI builds them), shipped, mine"
	@echo "make publish   PROFILE=name    CI builds this shipped profile (# publish: yes; commit it)"
	@echo "make unpublish PROFILE=name    CI no longer builds it"
	@echo "make share     PROFILE=name    move one of my profiles (config/my-profiles) to config/profiles"
	@echo "make delete    PROFILE=name    delete a profile (mine first, else the shipped one; never the default)"
	@echo "make web      [PORT=8766] [SIM=1] the site on 127.0.0.1 (installer, editor, builder, simulator; no firmware in it), open the editor (Chrome or Edge)"
	@echo "make site    [SITE=build/site] [PKG=release.fwsc] [VERSION=X.Y] the site without firmware (as make web), and with PKG (a release build: tools/build.py --release) the release's own site in SITE/release/: install page with m-bootloader, manual, emulator"
	@echo "make emu     [FW=name] [CPU=96|own] [FRESH=1] run a firmware in the emulator (asks when FW is empty);"
	@echo "                              its saved flash is kept between runs (emulator/state/), FRESH=1 starts without it"
	@echo "make emu-list                  list the firmware the emulator finds (build/ and $(IMAGES)/)"
	@echo "make emu-update                fetch and rebuild the emulator (emulator/fm1-emulator)"
	@echo "make test   [PROFILE=name]    the host tests: Optimist's (on the last classic build; with PROFILE: build it first), then"
	@echo "                              the release's (fm1base/optimist/run.sh none + dev, fm1base/tests/run_tests.sh)"
	@echo "make costs                     measure what tools/builder/costs.json lacks (new items); run after merging a batch"
	@echo "make costs-all [JOBS=n]        measure every item again (flash, RAM, pool, RAMTEXT; ~440 builds, JOBS at once,"
	@echo "                              default the CPUs and the free RAM; a progress line with the time left) for the terminal and"
	@echo "                              the web builder (tools/builder/costs.json, web/builder/data/); commit both"
	@echo "make gate  [CONFIG=f] [SET=\"K=V ..\"] [EMU=1]  the integrator's gate: costs, every published profile, the host"
	@echo "                              tests, a final build of the default profile (EMU=1: then emu-check)"
	@echo "make emu-check [PKG=file.fwsc] [ONLY=boot,..]  the emulator smoke checks on the last build (needs Rust; BUILDING.md)"
	@echo "make mirror                    build the Optimist Mirror app for this Mac and start it (again: the running one quits;"
	@echo "                              Xcode, the team in ios/OptimistMirror/Local.xcconfig)"
	@echo "The same without make: $(PY) tools/optimist.py --help"
	@echo "Put downloaded firmware (.fwsc) in $(IMAGES)/; it is git-ignored."

setup:
	$(PY) tools/optimist.py setup

# the AC79 SDK files (./sdk, git-ignored): fetched when missing or not the pinned version's (sha256);
# an SDK elsewhere (AC79_SDK, ~/fw-AC79_AIoT_SDK) is only completed, never overwritten;
# in a git worktree it is the main checkout's sdk/ (fetched there once; BUILDING.md, Worktrees)
sdk:
	@$(PY) -c "import sys; sys.path.insert(0, 'tools'); import toolchain as t, deps; d = t.sdk_dir(); (d == t.default_sdk_dir() or t.sdk_missing(d)) and deps.fetch_sdk(d)"

builder: sdk
	$(PY) tools/optimist.py builder $(CL)

build: sdk
	$(PY) tools/optimist.py build --profile $(PROFILE) $(CL)

# Optimist's own firmware, as make build made it before the release (what main ships)
classic: sdk
	$(PY) tools/optimist.py build --classic --profile $(PROFILE)

package: sdk
	$(PY) tools/optimist.py package --profile $(PROFILE) --out $(IMAGES) $(CL)

sd-zip:
	$(PY) tools/sd_zip.py build/optimist-sd-card.zip

# (builds PROFILE first unless PKG is given, so it never flashes an old file; asks first. Needs mido + python-rtmidi:
# make setup)
flash:
	$(PY) tools/optimist.py flash $(if $(PKG),$(PKG),--profile $(PROFILE)) $(if $(FM1),--port "$(FM1)") $(if $(INSTALL_MBOOT),--install-mboot) $(CL)

profiles:
	$(PY) tools/optimist.py config --profiles

# (these need PROFILE= on the command line: never the default by accident)
publish unpublish share delete:
	$(if $(filter file,$(origin PROFILE)),$(error make $@ PROFILE=name (make profiles lists them)))
	$(PY) tools/optimist.py config --$@ "$(PROFILE)"

web:
	$(PY) tools/optimist.py web $(if $(PORT),--port $(PORT)) --profile $(PROFILE) $(if $(SIM),--sim)

site:
	$(PY) web/make_site.py --local $(or $(VERSION),$(shell cat VERSION)) $(or $(SITE),build/site) $(if $(PKG),--release-pkg $(PKG))

emu:
	IMAGES=$(abspath $(IMAGES)) $(PY) tools/optimist.py emu $(FW) $(if $(CPU),--cpu $(CPU)) $(if $(FRESH),--fresh)

emu-list:
	IMAGES=$(abspath $(IMAGES)) $(PY) tools/optimist.py emu --list

emu-update:
	$(PY) tools/optimist.py emu --update

test: sdk
ifeq ($(origin PROFILE),file)
	@if [ -f build/felucca.fwsc ]; then echo "testing the last build (make test PROFILE=x builds x first)"; $(PY) tools/optimist.py test --no-build; else $(PY) tools/optimist.py test; fi
else
	$(PY) tools/optimist.py build --classic --profile $(PROFILE) && $(PY) tools/optimist.py test --no-build
endif

costs: sdk
	$(PY) tools/optimist.py costs $(CL)
	$(PY) tools/export_builder_web.py

# (every item, pair, group and check configuration again: what --missing keeps when an item's code changed)
costs-all: sdk
	$(PY) tools/optimist.py costs $(CL) $(if $(JOBS),--jobs $(JOBS),$(if $(CLASSIC),,--jobs 1))
	$(PY) tools/export_builder_web.py

# (tools/gate.py --help)
gate: sdk
	$(PY) tools/optimist.py gate $(if $(CONFIG),--config $(CONFIG)) $(foreach kv,$(SET),--set $(kv)) $(if $(EMU),--emu)

# (opt-in: Rust and the emulator; tests/emu/run.py --help. PKG: a package with its ELF beside it)
emu-check:
	$(PY) tests/emu/run.py $(PKG) $(if $(ONLY),--only $(ONLY))

MIRROR_APP = build/mirror/Build/Products/Debug-maccatalyst/OptimistMirror.app
mirror:
	xcodebuild -project ios/OptimistMirror/OptimistMirror.xcodeproj -scheme OptimistMirror -configuration Debug \
		-destination 'platform=macOS,variant=Mac Catalyst' -derivedDataPath build/mirror -allowProvisioningUpdates -quiet build
	@pkill -f "$(MIRROR_APP)/Contents/MacOS/OptimistMirror" || true
	open "$(MIRROR_APP)"
