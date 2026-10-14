# PC-side tests. Need src/dnas/baked_data.h (run `make` at the top level, or
# `sh ../tools/gen_baked.sh`).
#
#   make saves_test   backup/restore/install tests, with ASan and UBSan
#   make ui_preview   screenshots of the UI

CC ?= cc
CORE = ../src/app/install.c ../src/app/saves.c ../src/app/psu.c \
       ../src/crypto/sha1.c ../src/crypto/des.c ../src/crypto/rc6.c \
       ../src/dnas/outer_key.c ../src/dnas/savecodec.c ../src/dnas/patcher.c
FLAGS = -O1 -g -std=gnu99 -Wall -Wextra -Wno-unused-parameter -I../src

all: saves_test ui_preview

saves_test: saves_test.c fake_io.c fake_io.h $(CORE)
	$(CC) $(FLAGS) -fsanitize=address,undefined -o $@ saves_test.c fake_io.c $(CORE)

ui_main.o: ../src/main.c ../src/ui/atlas_data.h
	$(CC) $(FLAGS) -Wno-unused-function -Ishim -Dmain=app_main -c -o $@ ../src/main.c

ui_preview: ui_preview.c fake_io.c fake_io.h ui_main.o $(CORE)
	$(CC) $(FLAGS) -Ishim -o $@ ui_preview.c fake_io.c ui_main.o $(CORE)

clean:
	rm -f saves_test ui_preview ui_main.o

.PHONY: all clean
