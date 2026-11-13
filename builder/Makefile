# Builds the game module (swr_game.dll / swrdc_game.dll) from the C sources generated from YOUR ROM.
# Used by build_game.ps1 (Windows, llvm-mingw downloaded by the script).
CC = x86_64-w64-mingw32-clang
CFLAGS = -O2 -fno-strict-aliasing -fexceptions -w -DNDEBUG -Isdk
MACHINE ?= hikaru
OUT ?= ../swr_game.dll
SRCS = $(sort $(wildcard gen/blk_*.c))
OBJS = $(addprefix obj/,$(notdir $(SRCS:.c=.o))) obj/blk_module_game.o

all: $(OUT)
obj/%.o: gen/%.c
	$(CC) $(CFLAGS) -c $< -o $@
obj/blk_module_game.o: sdk/blk_module_game.c
	$(CC) $(CFLAGS) $(if $(filter dreamcast,$(MACHINE)),-DSWR_DREAMCAST) -c $< -o $@
$(OUT): $(OBJS)
	$(CC) -shared -static -o $@ $(OBJS) -Wl,--exclude-all-symbols -s
