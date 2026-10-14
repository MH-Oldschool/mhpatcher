# mhpatcher. Run `. ./env.sh` first.
#
#   make          build/mhpatcher.elf
#   make iso      build/mhpatcher.iso
#   make hostfs   build/mhpatcher-hostfs.elf, PCSX2 host: folder instead of USB
#   make atlas    regenerate src/ui/atlas_data.h (Python 3, Pillow, Noto Sans CJK)

ifeq ($(PS2SDK),)
$(error run `. ./env.sh` first)
endif

EE_CC  := mips64r5900el-ps2-elf-gcc
EE_BIN := build/mhpatcher.elf

INCS := -Isrc -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(GSKIT)/include
CFLAGS := -D_EE -G0 -O2 -Wall -Wextra -Wno-unused-parameter $(INCS) $(EXTRA_CFLAGS)
LDFLAGS := -T$(PS2SDK)/ee/startup/linkfile -L$(GSKIT)/lib -L$(PS2SDK)/ee/lib \
           -Wl,-zmax-page-size=128
LIBS := -lgskit -ldmakit -lmc -lpad -lcdvd -lfileXio -lpatches -lkernel -lc -lkernel

SRCS := src/main.c \
        src/crypto/sha1.c src/crypto/des.c src/crypto/rc6.c \
        src/dnas/outer_key.c src/dnas/savecodec.c src/dnas/patcher.c \
        src/ps2/video.c src/ps2/pad.c src/ps2/nvm_ilink.c src/ps2/mcard.c src/ps2/usb.c \
        src/app/install.c src/app/psu.c src/app/saves.c

# each variant has its own object directory
OBJDIR := build/obj$(VARIANT)
OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

# USB drivers embedded in the ELF
IRX_NAMES := iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd
OBJS      += $(patsubst %,build/irx/%_irx.o,$(IRX_NAMES))

all: $(EE_BIN)

$(EE_BIN): $(OBJS)
	$(EE_CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
	mips64r5900el-ps2-elf-strip $@

$(OBJDIR)/%.o: %.c src/dnas/baked_data.h
	@mkdir -p $(dir $@)
	$(EE_CC) $(CFLAGS) -c $< -o $@

build/irx/%_irx.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p build/irx
	bin2c $< $@ $*_irx

build/irx/%_irx.o: build/irx/%_irx.c
	$(EE_CC) $(CFLAGS) -c $< -o $@

src/dnas/baked_data.h: tools/gen_baked.sh $(wildcard data/*)
	sh tools/gen_baked.sh

atlas:
	python3 -I tools/make_atlas.py

iso: $(EE_BIN)
	python3 -I tools/mkiso.py $(EE_BIN) build/mhpatcher.iso

hostfs:
	$(MAKE) VARIANT=-hostfs EXTRA_CFLAGS=-DUSB_HOSTFS EE_BIN=build/mhpatcher-hostfs.elf

clean:
	rm -rf build src/dnas/baked_data.h

.PHONY: all atlas iso hostfs clean
