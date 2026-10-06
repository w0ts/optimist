# SPDX-License-Identifier: GPL-3.0-only
# Shortcuts. `make help` lists them; the scripts behind them have their own --help.
.DEFAULT_GOAL := help
.PHONY: help builder build package emu emu-list emu-update test

PROFILE ?= user-default
FW ?=
CPU ?=
IMAGES ?= firmwares

help:
	@echo "make builder                   the firmware builder menu (pick features, build: build/optimist-<version>-*.fwsc)"
	@echo "make build   [PROFILE=name]    build a profile without the menu ($(PROFILE))"
	@echo "make package [PROFILE=name]    build a profile and copy .fwsc + -ui.zip into $(IMAGES)/"
	@echo "make emu     [FW=name] [CPU=96] run a firmware in the emulator (asks when FW is empty)"
	@echo "make emu-list                  list the firmware the emulator finds (build/ and $(IMAGES)/)"
	@echo "make emu-update                fetch and rebuild the emulator (.emu/fm1-emulator)"
	@echo "make test                      the host test suite"
	@echo "Put downloaded firmware (.fwsc) in $(IMAGES)/; it is git-ignored."

builder:
	tools/menuconfig

build:
	tools/menuconfig --profile $(PROFILE) --build

package:
	mkdir -p $(IMAGES)
	tools/menuconfig --profile $(PROFILE) --package $(IMAGES)

emu:
	IMAGES=$(abspath $(IMAGES)) tools/emu.sh $(FW) $(if $(CPU),--cpu $(CPU))

emu-list:
	IMAGES=$(abspath $(IMAGES)) tools/emu.sh --list

emu-update:
	tools/emu.sh --update

test:
	sh tests/run_tests.sh
