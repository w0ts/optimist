# SPDX-License-Identifier: GPL-3.0-only
# Shortcuts for tools/optimist.py (which works without make: BUILDING.md). `make help` lists them.
.DEFAULT_GOAL := help
.PHONY: help setup sdk builder build package profiles publish unpublish share delete emu emu-list emu-update test costs

PY ?= python3
PROFILE ?= user-default
FW ?=
CPU ?=
IMAGES ?= firmwares

help:
	@echo "make setup                     check and fetch what the build and the emulator need"
	@echo "make sdk                       fetch the AC79 SDK files into ./sdk when missing or stale (build, builder, package, test run it first)"
	@echo "make builder                   the firmware builder menu (pick features, build: build/optimist-<version>-*.fwsc)"
	@echo "make build   [PROFILE=name]    build a profile without the menu ($(PROFILE))"
	@echo "make package [PROFILE=name]    build a profile and copy .fwsc + -ui.zip into $(IMAGES)/"
	@echo "make profiles                  the profiles: published (CI builds them), shipped, mine"
	@echo "make publish   PROFILE=name    CI builds this shipped profile (# publish: yes; commit it)"
	@echo "make unpublish PROFILE=name    CI no longer builds it"
	@echo "make share     PROFILE=name    move one of my profiles (config/my-profiles) to config/profiles"
	@echo "make delete    PROFILE=name    delete a profile (mine first, else the shipped one; never user-default)"
	@echo "make emu     [FW=name] [CPU=96|own] [FRESH=1] run a firmware in the emulator (asks when FW is empty);"
	@echo "                              its saved flash is kept between runs (emulator/state/), FRESH=1 starts without it"
	@echo "make emu-list                  list the firmware the emulator finds (build/ and $(IMAGES)/)"
	@echo "make emu-update                fetch and rebuild the emulator (emulator/fm1-emulator)"
	@echo "make test   [PROFILE=name]    the host tests on the last build (with PROFILE: build it first)"
	@echo "make costs                     measure what tools/builder/costs.json lacks (new items); run after merging a batch"
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
	$(PY) tools/optimist.py builder

build: sdk
	$(PY) tools/optimist.py build --profile $(PROFILE)

package: sdk
	$(PY) tools/optimist.py package --profile $(PROFILE) --out $(IMAGES)

profiles:
	$(PY) tools/optimist.py config --profiles

# (these need PROFILE= on the command line: never the default by accident)
publish unpublish share delete:
	$(if $(filter file,$(origin PROFILE)),$(error make $@ PROFILE=name (make profiles lists them)))
	$(PY) tools/optimist.py config --$@ "$(PROFILE)"

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
	$(PY) tools/optimist.py build --profile $(PROFILE) && $(PY) tools/optimist.py test --no-build
endif

costs:
	$(PY) tools/optimist.py costs
