# SPDX-License-Identifier: GPL-3.0-only
# Shortcuts for tools/optimist.py (which works without make: BUILDING.md). `make help` lists them.
.DEFAULT_GOAL := help
.PHONY: help setup builder build package emu emu-list emu-update test

PY ?= python3
PROFILE ?= user-default
FW ?=
CPU ?=
IMAGES ?= firmwares

help:
	@echo "make setup                     check and fetch what the build and the emulator need"
	@echo "make builder                   the firmware builder menu (pick features, build: build/optimist-<version>-*.fwsc)"
	@echo "make build   [PROFILE=name]    build a profile without the menu ($(PROFILE))"
	@echo "make package [PROFILE=name]    build a profile and copy .fwsc + -ui.zip into $(IMAGES)/"
	@echo "make emu     [FW=name] [CPU=96|own] run a firmware in the emulator (asks when FW is empty)"
	@echo "make emu-list                  list the firmware the emulator finds (build/ and $(IMAGES)/)"
	@echo "make emu-update                fetch and rebuild the emulator (emulator/fm1-emulator)"
	@echo "make test   [PROFILE=name]    the host tests on the last build (with PROFILE: build it first)"
	@echo "The same without make: $(PY) tools/optimist.py --help"
	@echo "Put downloaded firmware (.fwsc) in $(IMAGES)/; it is git-ignored."

setup:
	$(PY) tools/optimist.py setup

builder:
	$(PY) tools/optimist.py builder

build:
	$(PY) tools/optimist.py build --profile $(PROFILE)

package:
	$(PY) tools/optimist.py package --profile $(PROFILE) --out $(IMAGES)

emu:
	IMAGES=$(abspath $(IMAGES)) $(PY) tools/optimist.py emu $(FW) $(if $(CPU),--cpu $(CPU))

emu-list:
	IMAGES=$(abspath $(IMAGES)) $(PY) tools/optimist.py emu --list

emu-update:
	$(PY) tools/optimist.py emu --update

test:
ifeq ($(origin PROFILE),file)
	@if [ -f build/felucca.fwsc ]; then echo "testing the last build (make test PROFILE=x builds x first)"; $(PY) tools/optimist.py test --no-build; else $(PY) tools/optimist.py test; fi
else
	$(PY) tools/optimist.py build --profile $(PROFILE) && $(PY) tools/optimist.py test --no-build
endif
