#include "sys.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cart.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"

#define NES_LOG_MAX 1024

static NES_LogCallback NES_LOG;

struct NES {
	struct sys {
		uint8_t ram[0x800];
		uint8_t open_bus;
		uint64_t cycle;
		uint64_t cycle_2007;
		bool write;

		struct {
			bool oam_begin;
			bool dmc_begin;
			bool oam;
			uint16_t oam_cycle;
			uint16_t dmc_addr;
			uint8_t dmc_delay;
		} dma;
	} sys;

	struct ctrl {
		bool strobe;
		uint32_t state[2];
		uint32_t bits[2];
		uint8_t buttons[4];
		uint8_t safe_buttons[4];
	} ctrl;

	struct cart *cart;
	struct cpu *cpu;
	struct ppu *ppu;
	struct apu *apu;
};


// Input

static void ctrl_set_button_state(struct ctrl *ctrl, uint8_t player, uint8_t state)
{
	switch (player) {
		case 0:
			ctrl->state[0] = ((ctrl->state[0] & 0x00FFFF00) | (0x8 << 16) | state);
			break;
		case 1:
			ctrl->state[1] = ((ctrl->state[1] & 0x00FFFF00) | (0x4 << 16) | state);
			break;
		case 2:
			ctrl->state[0] = ((ctrl->state[0] & 0x000000FF) | (0x8 << 16) | (state << 8));
			break;
		case 3:
			ctrl->state[1] = ((ctrl->state[1] & 0x000000FF) | (0x4 << 16) | (state << 8));
			break;
	}
}

static void ctrl_set_safe_state(struct ctrl *ctrl, uint8_t player)
{
	uint8_t prev_state = ctrl->safe_buttons[player];
	ctrl->safe_buttons[player] = ctrl->buttons[player];

	// Cancel out up + down
	if ((ctrl->safe_buttons[player] & 0x30) == 0x30)
		ctrl->safe_buttons[player] &= 0xCF;

	// Cancel out left + right
	if ((ctrl->safe_buttons[player] & 0xC0) == 0xC0)
		ctrl->safe_buttons[player] &= 0x3F;

	if (prev_state != ctrl->safe_buttons[player])
		ctrl_set_button_state(ctrl, player, ctrl->safe_buttons[player]);
}

static uint8_t ctrl_read(struct ctrl *ctrl, uint8_t n)
{
	if (ctrl->strobe)
		return 0x40 | (ctrl->state[n] & 1);

	uint8_t r = 0x40 | (ctrl->bits[n] & 1);
	ctrl->bits[n] = (n < 2 ? 0x80 : 0x80000000) | (ctrl->bits[n] >> 1);

	return r;
}

static void ctrl_write(struct ctrl *ctrl, bool strobe)
{
	if (ctrl->strobe && !strobe) {
		ctrl->bits[0] = ctrl->state[0];
		ctrl->bits[1] = ctrl->state[1];
	}

	ctrl->strobe = strobe;
}


// IO
// https://wiki.nesdev.com/w/index.php/CPU_memory_map

uint8_t sys_read(NES *nes, uint16_t addr)
{
	if (addr < 0x2000) {
		return nes->sys.ram[addr % 0x0800];

	} else if (addr < 0x4000) {
		addr = 0x2000 + addr % 8;

		// Double 2007 read glitch and mapper 185 copy protection
		if (addr == 0x2007 && (nes->sys.cycle - nes->sys.cycle_2007 == 1 || cart_block_2007(nes->cart)))
			return ppu_read(nes->ppu, nes->cart, 0x2003);

		nes->sys.cycle_2007 = nes->sys.cycle;
		return ppu_read(nes->ppu, nes->cart, addr);

	} else if (addr == 0x4015) {
		nes->sys.open_bus = apu_read_status(nes->apu, false);
		return nes->sys.open_bus;

	} else if (addr == 0x4016 || addr == 0x4017) {
		nes->sys.open_bus = ctrl_read(&nes->ctrl, addr & 1);
		return nes->sys.open_bus;

	} else if (addr >= 0x4020) {
		bool hit = false;
		uint8_t v = cart_prg_read(nes->cart, nes->apu, addr, &hit);

		if (hit)
			return v;
	}

	return nes->sys.open_bus;
}

void sys_write(NES *nes, uint16_t addr, uint8_t v)
{
	if (addr < 0x2000) {
		nes->sys.ram[addr % 0x800] = v;

	} else if (addr < 0x4000) {
		addr = 0x2000 + addr % 8;

		ppu_write(nes->ppu, nes->cart, addr, v);
		cart_ppu_write_hook(nes->cart, addr, v); //MMC5 listens here

	} else if (addr < 0x4014 || addr == 0x4015 || addr == 0x4017) {
		nes->sys.open_bus = v;
		apu_write(nes->apu, nes, addr, v, false);

	} else if (addr == 0x4014) {
		nes->sys.open_bus = v;
		nes->sys.dma.oam_begin = true;

	} else if (addr == 0x4016) {
		nes->sys.open_bus = v;
		ctrl_write(&nes->ctrl, v & 1);

	} else if (addr < 0x4020) {
		nes->sys.open_bus = v;

	} else {
		cart_prg_write(nes->cart, nes->apu, addr, v);
	}
}


// DMA

static void sys_dma_oam(NES *nes, uint8_t v)
{
	// https://forums.nesdev.com/viewtopic.php?f=3&t=6100

	if (!nes->sys.dma.oam_begin)
		return;

	nes->sys.dma.oam_begin = false;
	nes->sys.dma.oam = true;
	cpu_halt(nes->cpu, true);

	sys_cycle(nes); // +1 default case

	if (sys_odd_cycle(nes)) // +1 if odd cycle
		sys_cycle(nes);

	// +512 read/write
	for (nes->sys.dma.oam_cycle = 0; nes->sys.dma.oam_cycle < 256; nes->sys.dma.oam_cycle++)
		sys_write_cycle(nes, 0x2014, sys_read_cycle(nes, v * 0x0100 + nes->sys.dma.oam_cycle));

	cpu_halt(nes->cpu, false);
	nes->sys.dma.oam = false;
}

void sys_dma_dmc_begin(NES *nes, uint16_t addr)
{
	nes->sys.dma.dmc_begin = true;
	nes->sys.dma.dmc_addr = addr;

	if (nes->sys.dma.oam) {
		if (nes->sys.dma.oam_cycle == 254) { // +0 second-to-second-to-last OAM cycle
			nes->sys.dma.dmc_delay = 0;

		} else if (nes->sys.dma.oam_cycle == 255) { // +2 last OAM cycle
			nes->sys.dma.dmc_delay = 2;

		} else { // +1 otherwise during OAM DMA
			nes->sys.dma.dmc_delay = 1;
		}
	} else if (nes->sys.write) { // +2 if CPU is writing
		nes->sys.dma.dmc_delay = 2;

	} else { // +3 default case
		nes->sys.dma.dmc_delay = 3;
	}
}

static uint8_t sys_dma_dmc(NES *nes, uint16_t addr, uint8_t v)
{
	if (!nes->sys.dma.dmc_begin)
		return v;

	if (addr == 0x2007) {
		nes->sys.cycle_2007 = 0;
		ppu_read(nes->ppu, nes->cart, addr);
	}

	v = sys_read(nes, addr);

	nes->sys.dma.dmc_begin = false;
	cpu_halt(nes->cpu, true);

	for (uint8_t x = 0; x < nes->sys.dma.dmc_delay; x++)
		sys_cycle(nes);

	apu_dma_dmc_finish(nes->apu, sys_read_cycle(nes, nes->sys.dma.dmc_addr));

	cpu_halt(nes->cpu, false);

	return v;
}


// Step

uint8_t sys_read_cycle(NES *nes, uint16_t addr)
{
	ppu_step(nes->ppu, nes->cart);

	uint8_t v = sys_read(nes, addr);

	ppu_step(nes->ppu, nes->cart);
	ppu_assert_nmi(nes->ppu, nes->cpu);

	cart_step(nes->cart, nes->cpu, nes->apu);
	cpu_poll_interrupts(nes->cpu);

	apu_step(nes->apu, nes);
	apu_assert_irqs(nes->apu, nes->cpu);

	nes->sys.cycle++;

	ppu_step(nes->ppu, nes->cart);

	// DMC DMA will engage after then next read tick
	return sys_dma_dmc(nes, addr, v);
}

void sys_write_cycle(NES *nes, uint16_t addr, uint8_t v)
{
	// DMC DMA will only engage on a read cycle, double writes will stall longer
	if (nes->sys.dma.dmc_begin)
		nes->sys.dma.dmc_delay++;

	ppu_step(nes->ppu, nes->cart);

	nes->sys.write = true;

	sys_write(nes, addr, v);
	ppu_step(nes->ppu, nes->cart);
	ppu_assert_nmi(nes->ppu, nes->cpu);

	cart_step(nes->cart, nes->cpu, nes->apu);
	cpu_poll_interrupts(nes->cpu);

	apu_step(nes->apu, nes);
	apu_assert_irqs(nes->apu, nes->cpu);

	nes->sys.cycle++;
	nes->sys.write = false;

	ppu_step(nes->ppu, nes->cart);

	// OAM DMA will engage after the write tick
	sys_dma_oam(nes, v);
}

void sys_cycle(NES *nes)
{
	sys_read_cycle(nes, 0);
}

bool sys_odd_cycle(NES *nes)
{
	return nes->sys.cycle & 1;
}


// Cart

bool NES_LoadCart(NES *ctx, const void *rom, size_t romSize, const NES_CartDesc *hdr)
{
	cart_destroy(&ctx->cart);

	if (rom) {
		ctx->cart = cart_create(rom, romSize, hdr);
		if (ctx->cart)
			NES_Reset(ctx, true);
	}

	return ctx->cart ? true : false;
}

bool NES_CartLoaded(NES *ctx)
{
	return ctx->cart;
}


// Disks

bool NES_LoadDisks(NES *ctx, const void *bios, size_t biosSize, const void *disks, size_t disksSize)
{
	cart_destroy(&ctx->cart);

	if (bios && disks) {
		ctx->cart = cart_fds_create(bios, biosSize, disks, disksSize);
		if (ctx->cart)
			NES_Reset(ctx, true);
	}

	return ctx->cart ? true : false;
}

bool NES_SetDisk(NES *ctx, int8_t disk)
{
	if (!ctx->cart)
		return false;

	return cart_fds_set_disk(ctx->cart, disk);
}

int8_t NES_GetDisk(NES *ctx)
{
	if (!ctx->cart)
		return 0;

	return cart_fds_get_disk(ctx->cart);
}

uint8_t NES_GetNumDisks(NES *ctx)
{
	if (!ctx->cart)
		return 0;

	return cart_fds_get_num_disks(ctx->cart);
}


// Step

uint32_t NES_NextFrame(NES *ctx, NES_VideoCallback videoCallback,
	NES_AudioCallback audioCallback, void *opaque)
{
	if (!ctx->cart)
		return 0;

	uint64_t cycles = ctx->sys.cycle;
	bool cpu_ok = true;

	while (cpu_ok && !ppu_new_frame(ctx->ppu)) {
		cpu_ok = cpu_step(ctx->cpu, ctx);

		// Fire audio callback in batches for lower latency
		uint32_t count = apu_num_frames(ctx->apu);

		if (count > 0)
			audioCallback(apu_pop_frames(ctx->apu), count, opaque);
	}

	if (!cpu_ok) {
		NES_LoadCart(ctx, NULL, 0, NULL);

	} else {
		videoCallback(ppu_pixels(ctx->ppu), opaque);
	}

	return (uint32_t) (ctx->sys.cycle - cycles);
}


// Input

void NES_ControllerState(NES *nes, uint8_t player, uint8_t state)
{
	struct ctrl *ctrl = &nes->ctrl;

	ctrl->buttons[player] = state;
	ctrl_set_safe_state(ctrl, player);
}


// Configuration

void NES_SetConfig(NES *ctx, const NES_Config *cfg)
{
	apu_set_config(ctx->apu, cfg);
	ppu_set_config(ctx->ppu, cfg);
}


// SRAM

size_t NES_GetSRAMSize(NES *ctx)
{
	return ctx->cart ? cart_get_sram_size(ctx->cart) : 0;
}

void *NES_GetSRAM(NES *ctx)
{
	return ctx->cart ? cart_get_sram(ctx->cart) : NULL;
}


// Lifecycle

NES *NES_Create(const NES_Config *cfg)
{
	NES *ctx = calloc(1, sizeof(NES));

	ctx->cpu = cpu_create();
	ctx->ppu = ppu_create(cfg);
	ctx->apu = apu_create(cfg);

	return ctx;
}

void NES_Destroy(NES **nes)
{
	if (!nes || !*nes)
		return;

	NES *ctx = *nes;

	apu_destroy(&ctx->apu);
	ppu_destroy(&ctx->ppu);
	cpu_destroy(&ctx->cpu);
	cart_destroy(&ctx->cart);

	free(ctx);
	*nes = NULL;
}

void NES_Reset(NES *ctx, bool hard)
{
	if (!ctx->cart)
		return;

	struct sys prev = ctx->sys;

	memset(&ctx->sys, 0, sizeof(struct sys));
	memset(&ctx->ctrl, 0, sizeof(struct ctrl));

	if (!hard) {
		memcpy(ctx->sys.ram, prev.ram, 0x800);
	} else {
		cart_reset(ctx->cart);
	}

	ppu_reset(ctx->ppu);
	apu_reset(ctx->apu, ctx, hard);
	cpu_reset(ctx->cpu, ctx, hard);
}


// System state

static size_t sys_get_state_size(void)
{
	return sizeof(struct sys);
}

static bool sys_set_state(struct sys *sys, const void *state, size_t size)
{
	if (size < sys_get_state_size())
		return false;

	*sys = *((const struct sys *) state);

	return true;
}

static bool sys_get_state(struct sys *sys, void *state, size_t size)
{
	if (size < sys_get_state_size())
		return false;

	memcpy(state, sys, sizeof(struct sys));

	return true;
}


// Controller state

static size_t ctrl_get_state_size(void)
{
	return sizeof(struct ctrl);
}

static bool ctrl_set_state(struct ctrl *ctrl, const void *state, size_t size)
{
	if (size < ctrl_get_state_size())
		return false;

	*ctrl = *((const struct ctrl *) state);

	return true;
}

static bool ctrl_get_state(struct ctrl *ctrl, void *state, size_t size)
{
	if (size < ctrl_get_state_size())
		return false;

	memcpy(state, ctrl, sizeof(struct ctrl));

	return true;
}


// State

size_t NES_GetStateSize(NES *ctx)
{
	if (!ctx->cart)
		return 0;

	return apu_get_state_size() + cart_get_state_size(ctx->cart) + cpu_get_state_size() +
		ppu_get_state_size() + sys_get_state_size() + ctrl_get_state_size();
}

bool NES_SetState(NES *ctx, const void *state, size_t size)
{
	if (!ctx->cart)
		return false;

	bool r = true;
	const uint8_t *s8 = state;

	size_t current_size = NES_GetStateSize(ctx);
	void *current = calloc(current_size, 1);
	NES_GetState(ctx, current, current_size);

	r = cpu_set_state(ctx->cpu, s8, size);
	if (!r)
		goto except;

	s8 += cpu_get_state_size();
	size -= cpu_get_state_size();

	r = apu_set_state(ctx->apu, s8, size);
	if (!r)
		goto except;

	s8 += apu_get_state_size();
	size -= apu_get_state_size();

	r = ppu_set_state(ctx->ppu, s8, size);
	if (!r)
		goto except;

	s8 += ppu_get_state_size();
	size -= ppu_get_state_size();

	r = cart_set_state(ctx->cart, s8, size);
	if (!r)
		goto except;

	s8 += cart_get_state_size(ctx->cart);
	size -= cart_get_state_size(ctx->cart);

	r = sys_set_state(&ctx->sys, s8, size);
	if (!r)
		goto except;

	s8 += sys_get_state_size();
	size -= sys_get_state_size();

	r = ctrl_set_state(&ctx->ctrl, s8, size);
	if (!r)
		goto except;

	s8 += ctrl_get_state_size();
	size -= ctrl_get_state_size();

	except:

	if (!r)
		NES_SetState(ctx, current, current_size);

	free(current);

	return r;
}

bool NES_GetState(NES *ctx, void *state, size_t size)
{
	if (!ctx->cart)
		return false;

	uint8_t *s8 = state;

	if (!cpu_get_state(ctx->cpu, s8, size))
		return false;

	s8 += cpu_get_state_size();
	size -= cpu_get_state_size();

	if (!apu_get_state(ctx->apu, s8, size))
		return false;

	s8 += apu_get_state_size();
	size -= apu_get_state_size();

	if (!ppu_get_state(ctx->ppu, s8, size))
		return false;

	s8 += ppu_get_state_size();
	size -= ppu_get_state_size();

	if (!cart_get_state(ctx->cart, s8, size))
		return false;

	s8 += cart_get_state_size(ctx->cart);
	size -= cart_get_state_size(ctx->cart);

	if (!sys_get_state(&ctx->sys, s8, size))
		return false;

	s8 += sys_get_state_size();
	size -= sys_get_state_size();

	if (!ctrl_get_state(&ctx->ctrl, s8, size))
		return false;

	s8 += ctrl_get_state_size();
	size -= ctrl_get_state_size();

	return true;
}


// Logging

void NES_SetLogCallback(NES_LogCallback log_callback)
{
	NES_LOG = log_callback;
}

void NES_Log(const char *fmt, ...)
{
	if (NES_LOG) {
		va_list args;
		va_start(args, fmt);

		char str[NES_LOG_MAX];
		char nfmt[NES_LOG_MAX];
		snprintf(nfmt, NES_LOG_MAX, "%s\n", fmt);
		vsnprintf(str, NES_LOG_MAX, nfmt, args);

		va_end(args);

		NES_LOG(str);
	}
}
