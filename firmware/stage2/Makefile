# SPDX-License-Identifier: GPL-3.0-only
# MENU.BIN, the SD flasher (s2_main.c): make -C firmware/stage2, or make stage2 at the top. Output in build/stage2/.
ROOT := $(abspath ../..)
TOOLCHAIN ?= $(firstword $(wildcard $(ROOT)/build/toolchains/pi32v2-llvm-dropin-*/llvm))
CLANG := $(TOOLCHAIN)/bin/clang
LINK := $(TOOLCHAIN)/bin/ld.lld
OBJCOPY := $(TOOLCHAIN)/bin/llvm-objcopy
MBOOT := $(ROOT)/third_party/m-bootloader
SD := $(ROOT)/firmware/src/storage/sd
HAL := $(ROOT)/firmware/hal
OUT := $(ROOT)/build/stage2

CFLAGS := -target pi32v2 -mcpu=r3 -std=c99 -Oz -ffreestanding -fno-builtin -fno-common -ffunction-sections -fdata-sections -Wall -Wextra -I$(MBOOT)/include -I. -I$(HAL) -I$(SD) -DPF_DIR_HOOK=s2_dir_hook
OBJS := s2_main s2_card s2_flash pff core platform display board runtime
OBJ_FILES := $(addprefix $(OUT)/,$(addsuffix .o,$(OBJS)))

vpath %.c . $(SD) $(MBOOT)/src

$(OUT)/MENU.BIN: $(OUT)/stage2.bin $(ROOT)/tools/stage2_pack.py
	python3 $(ROOT)/tools/stage2_pack.py $< $@

$(OUT)/stage2.bin: $(OUT)/stage2.elf
	$(OBJCOPY) -O binary -j .text $< $@

$(OUT)/stage2.elf: $(OBJ_FILES) $(OUT)/stage2.ld
	$(LINK) --relax --gc-sections -T $(OUT)/stage2.ld -Map $(OUT)/stage2.map -o $@ $(OBJ_FILES)

$(OUT)/stage2.ld: $(MBOOT)/tools/target.ld.S
	@mkdir -p $(@D)
	$(CLANG) -E -P -x c -DENTRY_SYMBOL=s2_entry -DRAM_ORIGIN=0x1c28000 -DRAM_LENGTH=0x8000 -DLOADER_BUILD $< -o $@

$(OUT)/%.o: %.c
	@mkdir -p $(@D)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(OUT)/s2_card.o: $(SD)/sd_mmc.c $(HAL)/fm1_sd_bus.h

clean:
	rm -rf $(OUT)

.PHONY: clean
