/* Star Wars Racer recomp, Dreamcast: our SH-4 core (interpreter + AOT blocks from the Hikaru recomp)
 * running as Flycast's SH-4 executor. Flycast supplies the machine (PowerVR2, GD-ROM, Maple, AICA, HLE
 * BIOS, interrupts and scheduler). Registers are mirrored between our Cpu and Flycast's Sh4Context at
 * every time slice (SH4_TIMESLICE cycles) and around interrupts, exceptions and HLE BIOS calls.
 *
 * Environment: SWRAOT_SEEN=<file> records the executed blocks (input of tools/blockgen.py);
 *              SWRAOT_NOAOT=1 runs only the interpreter. */
#include "types.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/mem/addrspace.h"
#include "reios/reios.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include "stdclass.h"

extern "C" {
#include "sh4.h"
#include "blockrt.h"
void sh4_init_tables(void);
void sh4_reset(Cpu *c);
}

int UpdateSystem_INTC();
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <climits>
#endif
#include "oslib/oslib.h"

/* The recompiled blocks are not part of the program: they come from the game module that the builder
 * (CONSTRUIR_JUEGO.bat / build_game.sh) compiles on the user's machine from their own disc image.
 * It is looked for next to the program, then in the configuration folder. */
static int load_game_module()
{
#ifdef _WIN32
	const char *name = "swrdc_game.dll";
	wchar_t w[1024]; char dir[3072] = "";
	DWORD n = GetModuleFileNameW(nullptr, w, 1024);
	if (n > 0 && n < 1024) {
		wchar_t *sl = wcsrchr(w, L'\\'); if (sl) sl[1] = 0;
		WideCharToMultiByte(CP_UTF8, 0, w, -1, dir, sizeof dir, nullptr, nullptr);
	}
#else
	const char *name = "libswrdc_game.so";
	char dir[PATH_MAX] = "";
	ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 1);
	if (n > 0) { dir[n] = 0; char *sl = strrchr(dir, '/'); if (sl) sl[1] = 0; } else dir[0] = 0;
#endif
	int r = dir[0] ? blk_module_load((std::string(dir) + name).c_str(), "dreamcast") : -1;
	if (r < 0) r = blk_module_load(get_writable_config_path(name).c_str(), "dreamcast");
	return r;
}

/* builder: SWRAOT_BUILDER_DUMP=<dir> writes main RAM to <dir>/ram_dc.bin one emulated second after the game
 * program (1ST_READ.BIN, loaded at 8C010000) starts running, then exits. The builder turns it into C. */
static void builder_dump(u32 pc, u64 cycles)
{
	static const char *dir = getenv("SWRAOT_BUILDER_DUMP");
	static u64 t0; static u64 delay = getenv("SWRAOT_BUILDER_DELAY") ? strtoull(getenv("SWRAOT_BUILDER_DELAY"), 0, 10) : 0;
	if (!dir) return;
	if (!t0) { if ((pc & 0x1fffffff) >= 0x0c010000 && (pc & 0x1fffffff) < 0x0d000000) t0 = cycles; return; }
	if (cycles - t0 < delay) return;
	FILE *f = fopen((std::string(dir) + "/ram_dc.bin").c_str(), "wb");
	int ok = f && fwrite(&mem_b[0], 1, 0x1000000, f) == 0x1000000;
	if (f) fclose(f);
	fflush(nullptr);
	_exit(ok ? 0 : 3);
}
extern "C" { extern u32 g_swr_watch; extern int g_swr_watch_n; }
extern u32 kcode[4];
extern u16 rt[4], lt[4];

/* test input script: SWRAOT_PRESS="sec:hexmask:len[:rt],..." (emulated seconds; mask = DC buttons, active
 * while sec <= t < sec+len; rt = right trigger 0..255) */
struct Press { double t, len; u32 mask; int rt; };
static Press presses[64]; static int npress = -1;
static void script_input(u64 cycles)
{
	if (npress < 0) {
		npress = 0;
		const char *e = getenv("SWRAOT_PRESS");
		while (e && *e && npress < 64) {
			Press p = { 0, 0, 0, 0 };
			int n = 0, rtv = 0;
			if (sscanf(e, "%lf:%x:%lf%n", &p.t, &p.mask, &p.len, &n) < 3) break;
			e += n;
			if (*e == ':') { sscanf(e + 1, "%d%n", &rtv, &n); e += 1 + n; p.rt = rtv; }
			presses[npress++] = p;
			if (*e == ',') e++;
		}
	}
	double t = (double)cycles / 200e6;
	/* debug: SWRAOT_DUMP="sec:file[,sec:file]" dumps main RAM; SWRAOT_POKE="sec:addr:hexval[:endsec],..." writes
	   a 32-bit value every ~1/60 s from sec on (addr = RAM offset) */
	{
		static int init; static struct { double t; char f[200]; int done; } dumps[8]; static int nd;
		static struct { double t, e; u32 a, v; } pokes[32]; static int np; static u64 last;
		if (!init) {
			init = 1;
			if (getenv("SWRAOT_WATCH")) g_swr_watch = strtoul(getenv("SWRAOT_WATCH"), 0, 16);
			const char *e = getenv("SWRAOT_DUMP");
			while (e && *e && nd < 8) { int n = 0; if (sscanf(e, "%lf:%199[^,]%n", &dumps[nd].t, dumps[nd].f, &n) < 2) break; nd++; e += n; if (*e == ',') e++; }
			e = getenv("SWRAOT_POKE");
			while (e && *e && np < 32) { int n = 0; pokes[np].e = 1e18; if (sscanf(e, "%lf:%x:%x%n", &pokes[np].t, &pokes[np].a, &pokes[np].v, &n) < 3) break; e += n;
				if (*e == ':') { sscanf(e + 1, "%lf%n", &pokes[np].e, &n); e += 1 + n; } np++; if (*e == ',') e++; }
		}
		for (int i = 0; i < nd; i++) if (!dumps[i].done && t >= dumps[i].t) { dumps[i].done = 1; FILE *f = fopen(dumps[i].f, "wb"); if (f) { fwrite(&mem_b[0], 1, 16 * 1024 * 1024, f); fclose(f); } }
		if (np && cycles - last > 3300000) { last = cycles; for (int i = 0; i < np; i++) if (t >= pokes[i].t && t < pokes[i].e) memcpy(&mem_b[pokes[i].a & 0xffffff], &pokes[i].v, 4); }
	}
	if (!npress) return;
	u32 m = 0; int r = 0;
	for (int i = 0; i < npress; i++)
		if (t >= presses[i].t && t < presses[i].t + presses[i].len) { m |= presses[i].mask; if (presses[i].rt > r) r = presses[i].rt; }
	kcode[0] = ~m; rt[0] = (u16)(r << 8);
}

static Cpu cpu;
static Sh4Context *ctx;
static int trace = -1;
static unsigned long n_reios, n_intr, n_exc, n_slices;

/* ---- register mirroring ---- */
static void to_fc()
{
	memcpy(ctx->r, cpu.r, sizeof ctx->r);
	memcpy(ctx->r_bank, cpu.rbank, sizeof ctx->r_bank);
	ctx->sr.setFull(cpu.sr);
	ctx->old_sr.status = ctx->sr.status;
	ctx->old_sr.RB &= ctx->sr.MD;
	SRdecode();     /* recompute the interrupt mask for this SR (IMASK / BL); Flycast's own copy may be stale */
	ctx->gbr = cpu.gbr; ctx->vbr = cpu.vbr; ctx->ssr = cpu.ssr; ctx->spc = cpu.spc;
	ctx->sgr = cpu.sgr; ctx->dbr = cpu.dbr; ctx->pr = cpu.pr; ctx->pc = cpu.pc;
	ctx->mac.h = cpu.mach; ctx->mac.l = cpu.macl; ctx->fpul = cpu.fpul;
	ctx->fpscr.full = cpu.fpscr; ctx->old_fpscr = ctx->fpscr;
	memcpy(ctx->fr, cpu.fpr.u[cpu.fb], sizeof ctx->fr);
	memcpy(ctx->xf, cpu.fpr.u[cpu.fb ^ 1], sizeof ctx->xf);
}
static void from_fc()
{
	memcpy(cpu.r, ctx->r, sizeof cpu.r);
	memcpy(cpu.rbank, ctx->r_bank, sizeof cpu.rbank);
	cpu.sr = ctx->sr.getFull();
	cpu.gbr = ctx->gbr; cpu.vbr = ctx->vbr; cpu.ssr = ctx->ssr; cpu.spc = ctx->spc;
	cpu.sgr = ctx->sgr; cpu.dbr = ctx->dbr; cpu.pr = ctx->pr; cpu.pc = ctx->pc;
	cpu.mach = ctx->mac.h; cpu.macl = ctx->mac.l; cpu.fpul = ctx->fpul;
	cpu.fpscr = ctx->fpscr.full; cpu.fb = (cpu.fpscr >> 21) & 1;
	memcpy(cpu.fpr.u[cpu.fb], ctx->fr, sizeof ctx->fr);
	memcpy(cpu.fpr.u[cpu.fb ^ 1], ctx->xf, sizeof ctx->xf);
}

/* ---- Flycast side of the bus (swraot_bus.c) ---- */
extern "C" {
u8 fc_read8(u32 a) { return addrspace::read8(a); }
u16 fc_read16(u32 a) { return addrspace::read16(a); }
u32 fc_read32(u32 a) { return addrspace::read32(a); }
void fc_write8(u32 a, u8 v) { addrspace::write8(a, v); }
void fc_write16(u32 a, u16 v) { addrspace::write16(a, v); }
void fc_write32(u32 a, u32 v) { addrspace::write32(a, v); }
void fc_sq_flush(Cpu *c, u32 a)
{
	int q = (a >> 5) & 1;
	memcpy(ctx->sq_buffer[q].data, c->sq[q], 32);
	ctx->doSqWrite(a, ctx);
}
void fc_reios_trap(Cpu *c, u32 pc)
{
	(void)c;
	cpu.pc = pc + 2;                 /* reios_trap expects pc = trap + 2 */
	to_fc();
	if (trace > 0 && n_reios < 60) fprintf(stderr, "SWRAOT reios trap pc=%08x pr=%08x r4=%08x r5=%08x r6=%08x r7=%08x\n", pc, cpu.pr, cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7]);
	n_reios++;
	reios_trap(ctx, 0x085b);
	from_fc();
	if (cpu.pc == pc + 2) cpu.pc = pc + 2;   /* unchanged: continue after the hook */
}
/* bulk RAM writes done by Flycast (DMA, GD-ROM, store queues): AOT blocks there must be re-verified */
void swraot_ram_written(u32 addr, u32 size)
{
	if (((addr & 0x1c000000u) == 0x0c000000u) && addr < 0xe0000000u)
		blk_ram_written(0, addr & 0x00ffffffu, size);
}
}

/* ---- widescreen (Star Wars Episode I Racer PAL, T23001D 50) ----
 * The camera set-up function at 8C0872B4 (r4 = camera) builds the projection from the horizontal FOV (+0x134,
 * degrees) and the pixel aspect (+0x138): the vertical FOV is derived from both. For 16:9 the hook widens the
 * horizontal FOV (tan * 4/3) and the aspect (* 4/3) while that function runs, which leaves the vertical FOV as
 * it was, and restores the fields when it returns. The picture is then stretched 4:3 -> 16:9 by Flycast
 * (ScreenStretching 134), only while the 3D camera is in use (videos and logos stay 4:3).
 * Enabled by Flycast's "Widescreen Game Cheats" option (rend.WidescreenGameHacks). */
#include "cfg/option.h"
#include <cmath>
static const u32 WS_FUNC = 0x8c0872b4;
static struct { bool armed; u32 ret, sp, cam; u32 fov, asp; int frames; bool checked, ok; } ws;
static inline u32 ramoff(u32 a) { return a & 0x00ffffff; }
static void ws_enter()
{
	if (!ws.checked) {
		ws.checked = true;
		static const u8 sig[8] = { 0xfb, 0xff, 0xeb, 0xff, 0xdb, 0xff, 0xcb, 0xff };   /* fmov.s fr15..fr12,@-r15 */
		u32 lit; memcpy(&lit, &mem_b[ramoff(0x8c0874c8)], 4);
		ws.ok = !memcmp(&mem_b[ramoff(WS_FUNC)], sig, 8) && lit == 0x138;
	}
	if (!ws.ok || ws.armed || !config::WidescreenGameHacks) return;
	u32 cam = cpu.r[4];
	if ((cam & 0x1f000000) != 0x0c000000) return;
	s32 vp[4]; memcpy(vp, &mem_b[ramoff(cam + 32)], 16);
	if (vp[3] - vp[1] < 400 || vp[2] - vp[0] < 560) return;         /* full-screen cameras only */
	float fov, asp;
	memcpy(&fov, &mem_b[ramoff(cam + 0x134)], 4); memcpy(&asp, &mem_b[ramoff(cam + 0x138)], 4);
	if (!(fov > 10.f && fov < 150.f) || !(asp > 0.5f && asp < 2.f)) return;
	const float k = 4.f / 3.f, d2r = 3.14159265f / 180.f;
	float nfov = 2.f * atanf(k * tanf(fov * 0.5f * d2r)) / d2r, nasp = asp * k;
	ws.armed = true; ws.ret = cpu.pr; ws.sp = cpu.r[15]; ws.cam = cam;
	memcpy(&ws.fov, &fov, 4); memcpy(&ws.asp, &asp, 4);
	memcpy(&mem_b[ramoff(cam + 0x134)], &nfov, 4); memcpy(&mem_b[ramoff(cam + 0x138)], &nasp, 4);
	ws.frames = 20;
}
static inline void ws_check()
{
	if (cpu.pc == WS_FUNC) ws_enter();
	else if (ws.armed && cpu.pc == ws.ret && cpu.r[15] == ws.sp) {
		memcpy(&mem_b[ramoff(ws.cam + 0x134)], &ws.fov, 4); memcpy(&mem_b[ramoff(ws.cam + 0x138)], &ws.asp, 4);
		ws.armed = false;
	}
}
static void ws_tick(u64 cycles)       /* about once per frame: stretch the output only while the 3D camera is active */
{
	static u64 last; static int cur = -1;
	if (cycles - last < 3300000) return;
	last = cycles;
	if (ws.frames > 0) ws.frames--;
	int want = (ws.frames > 0 && config::WidescreenGameHacks) ? 134 : 100;
	if (want != cur) { cur = want; config::ScreenStretching.override(want); }
}

class Sh4AotExecutor : public Sh4Executor
{
	bool told = false;
public:
	void Init() override
	{
		ctx = &p_sh4rcb->cntx;
		memset(ctx, 0, sizeof(*ctx));
		static bool once;
		if (!once) {
			once = true;
			sh4_init_tables();
			g_blk_ram[0] = g_blk_ram[1] = &mem_b[0];
			g_blk_ram_mask = 0x00ffffff;
			/* code run outside the AOT table is listed in swraot_misses.txt (next to the program): input
			   for the next blockgen pass (tools/blockgen.py --misses) */
			static std::string misses = getenv("SWRAOT_MISSES") ? getenv("SWRAOT_MISSES") : get_writable_config_path("swraot_misses.txt");
			if (!getenv("SWRAOT_NOAOT") && !getenv("SWRAOT_BUILDER_DUMP")) load_game_module();
			blk_init(getenv("SWRAOT_NOAOT") == nullptr, 0, misses.c_str(), getenv("SWRAOT_SEEN"));
			INFO_LOG(INTERPRETER, "SWRAOT: %d recompiled blocks", blk_table_size());
		}
	}
	void Term() override { blk_finish(); }
	void Reset(bool hard) override
	{
		ctx = &p_sh4rcb->cntx;
		if (hard) {
			int schedNext = ctx->sh4_sched_next;
			memset(ctx, 0, sizeof(*ctx));
			ctx->sh4_sched_next = schedNext;
		}
		ctx->pc = 0xA0000000;
		ctx->sr.setFull(0x700000F0);
		ctx->old_sr.status = ctx->sr.status;
		UpdateSR();
		ctx->fpscr.full = 0x00040001;
		ctx->old_fpscr = ctx->fpscr;
		ctx->cycle_counter = SH4_TIMESLICE;
		memset(&cpu, 0, sizeof cpu);
		sh4_reset(&cpu);
	}
	void Start() override { ctx->CpuRunning = true; }
	void Stop() override { ctx->CpuRunning = false; ctx->cycle_counter = 0; }
	bool IsCpuRunning() override { return ctx->CpuRunning; }
	void ResetCache() override {}
	void Step() override
	{
		from_fc();
		u64 c0 = cpu.cycles;
		blk_step(&cpu);
		ctx->cycle_counter -= (int)(cpu.cycles - c0);
		to_fc();
	}
	void Run() override
	{
		ctx->restoreHostRoundingMode();
		from_fc();
		do {
			try {
				/* one slice: run blocks until the cycle budget is used up */
				while (ctx->cycle_counter > 0 && ctx->CpuRunning) {
					if (cpu.sleeping) { ctx->cycle_counter = 0; break; }
					u64 c0 = cpu.cycles;
					ws_check();
					blk_step(&cpu);
					ctx->cycle_counter -= (int)(cpu.cycles - c0);
				}
				ctx->cycle_counter += SH4_TIMESLICE;
				ws_tick(cpu.cycles);
				script_input(cpu.cycles);
				builder_dump(cpu.pc, cpu.cycles);
				if (!told && cpu.cycles > 600000000ull) {
					told = true;
					if (blk_table_size() == 0 && !getenv("SWRAOT_BUILDER_DUMP"))
						os_notify("Interpreter / Intérprete", 8000, "Run BUILD_GAME / Ejecuta CONSTRUIR_JUEGO");
				}
				to_fc();
				u32 pc0 = ctx->pc;
				UpdateSystem_INTC();                 /* scheduler + interrupts (may vector the CPU) */
				from_fc();
				if (cpu.pc != pc0) { cpu.sleeping = 0; n_intr++; }  /* an interrupt wakes a sleeping CPU */
				if (trace < 0) trace = getenv("SWRAOT_TRACE") ? 1 : 0;
				if (trace > 0 && ++n_slices % 200000 == 0)
				{	fprintf(stderr, "SWRAOT slices=%lu pc=%08x pr=%08x sr=%08x sleep=%d reios=%lu intr=%lu exc=%lu\n", n_slices, cpu.pc, cpu.pr, cpu.sr, cpu.sleeping, n_reios, n_intr, n_exc);
					if (n_slices % 2000000 == 0) blk_stats(stderr); }
			} catch (const SH4ThrownException& ex) {
				n_exc++;
				if (trace > 0 && n_exc < 20) fprintf(stderr, "SWRAOT exception %x at %08x\n", (unsigned)ex.expEvn, ex.epc);
				to_fc();
				Do_Exception(ex.epc, ex.expEvn);
				from_fc();
				cpu.sleeping = 0;
			}
		} while (ctx->CpuRunning);
		to_fc();
		ctx->CpuRunning = false;
	}
};

Sh4Executor *Get_Sh4AotExecutor()
{
	static Sh4AotExecutor e;
	return &e;
}
