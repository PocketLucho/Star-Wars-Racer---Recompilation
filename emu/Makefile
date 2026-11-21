CFLAGS=-O2 -g -I../third_party/miniz -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-misleading-indentation
CORE=sh4interp.o hikaru.o x76f100.o gpu.o mie.o z80.o crypt5881.o blockrt.o blk_module.o hikaru_cp.o hikaru_gfx.o swrender.o romzip.o miniz.o arm7.o aica.o emu_core.o cheats.o main_cli.o
HDRS=sh4.h sh4exec.h hikaru.h optable.h x76f100.h gpu.h mie.h blockrt.h crypt5881.h hikaru_cp.h swrender.h hikaru_gfx.h gfx_out.h romzip.h emu_core.h arm7.h aica.h

# reference build: every block through the interpreter
hikaru_run: $(CORE) blk_stub.o
	$(CC) -o $@ $^ -lm -lz

# AOT build: generated blocks (gen/blk_*.c from tools/blockgen.py)
GEN_SRCS=$(sort $(wildcard gen/blk_*.c))
GEN_OBJS=$(GEN_SRCS:.c=.o)
hikaru_aot: $(CORE) $(GEN_OBJS)
	$(CC) -o $@ $^ -lm -lz
gen/%.o: gen/%.c sh4sem.h sh4exec.h sh4.h blockrt.h
	$(CC) -O2 -g0 -w -I. -c $< -o $@

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<
clean:
	rm -f *.o gen/*.o hikaru_run hikaru_aot

cpview: cpview.c swrender.c hikaru_cp.c hikaru_gfx.c hikaru_cp.h swrender.h hikaru_gfx.h
	$(CC) -O2 -w -o $@ cpview.c swrender.c hikaru_cp.c hikaru_gfx.c -lz -lm

# Vulkan renderer (Linux test build, headless). Shaders: tools/bin/glslang -> shaders/*.h
VKINC=-I../third_party/volk -I../third_party/Vulkan-Headers/include -DVK_NO_PROTOTYPES
shaders/hikaru_vert.h: shaders/hikaru.vert
	../tools/bin/glslang -V --vn hikaru_vert_spv -o $@ $<
shaders/hikaru_frag.h: shaders/hikaru.frag
	../tools/bin/glslang -V --vn hikaru_frag_spv -o $@ $<
shaders/overlay_vert.h: shaders/overlay.vert
	../tools/bin/glslang -V --vn overlay_vert_spv -o $@ $<
shaders/overlay_frag.h: shaders/overlay.frag
	../tools/bin/glslang -V --vn overlay_frag_spv -o $@ $<
shaders/kept_frag.h: shaders/kept.frag
	../tools/bin/glslang -V --vn kept_frag_spv -o $@ $<
vkrender.o: vkrender.c vkrender.h hikaru_gfx.h shaders/kept_frag.h shaders/hikaru_vert.h shaders/hikaru_frag.h shaders/overlay_vert.h shaders/overlay_frag.h
	$(CC) $(CFLAGS) $(VKINC) -c $<
volk.o: ../third_party/volk/volk.c
	$(CC) -O2 $(VKINC) -c $< -o $@
gfx_vk.o: gfx_vk.c vkrender.h gfx_out.h
	$(CC) $(CFLAGS) $(VKINC) -c $<
main_vk.o: main_cli.c $(HDRS) vkrender.h
	$(CC) $(CFLAGS) $(VKINC) -DHAVE_VULKAN -c main_cli.c -o $@
VKOBJS=vkrender.o volk.o gfx_vk.o
hikaru_aot_vk: $(filter-out main_cli.o,$(CORE)) main_vk.o $(VKOBJS) $(GEN_OBJS)
	$(CC) -o $@ $^ -lm -lz -ldl
miniz.o: ../third_party/miniz/miniz.c
	$(CC) -O2 -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -I../third_party/miniz -c $< -o $@

# Linux / Steam Deck frontend (SDL2 + Vulkan). SDL2 headers from third_party/SDL2 (release-2.30.0).
# By default it links the system libSDL2-2.0.so.0. The portable package (tools/package_linux.sh) passes
# SDL_STATIC=<prefix of a static SDL2 build>: SDL is then inside the binary and its video/audio backends
# (X11, Wayland, PipeWire, PulseAudio, ALSA) are loaded at run time, so nothing has to be installed.
SDL_STATIC ?=
ifeq ($(SDL_STATIC),)
SDLINC=-I../third_party/SDL2/include
SDLLIB=-l:libSDL2-2.0.so.0 -Wl,-rpath,'$$ORIGIN/lib'
else
SDLINC=-I$(SDL_STATIC)/include/SDL2
SDLLIB=$(SDL_STATIC)/lib/libSDL2.a -pthread
endif
osd.o: osd.c osd.h font8x16.h
	$(CC) $(CFLAGS) -c $<
main_sdl.o: main_sdl.c $(HDRS) vkrender.h osd.h
	$(CC) $(CFLAGS) $(VKINC) $(SDLINC) -c main_sdl.c -o $@
swracer: $(filter-out main_cli.o,$(CORE)) main_sdl.o osd.o $(VKOBJS) blk_stub.o
	$(CC) -o $@ $^ $(SDLLIB) -lm -lz -ldl
