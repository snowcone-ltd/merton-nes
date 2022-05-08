#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"
#include "apu.h"

#define RANGE_PRG  0
#define RANGE_CHR  1

#define MEM_ROM    0
#define MEM_RAM    1
#define MEM_CIRAM  2
#define MEM_EXRAM  3

#define MARK_SPR   0x11
#define MARK_BG    0x10

#define PRG_SRAM   0xFA

#define MAPPER_MAX 512

enum mem {
	PRG     = (RANGE_PRG << 2),
	CHR     = (RANGE_CHR << 2),
	PRG_ROM = PRG | MEM_ROM,
	PRG_RAM = PRG | MEM_RAM,
	CHR_ROM = CHR | MEM_ROM,
	CHR_RAM = CHR | MEM_RAM,
	CIRAM   = CHR | MEM_CIRAM,

	// MMC5
	EXRAM   = CHR | MEM_EXRAM,
	CHR_SPR = (MARK_SPR << 3) | CHR,
	CHR_BG  = (MARK_BG  << 3) | CHR,
};

struct cart;

// Map
void cart_map(struct cart *ctx, enum mem type, uint16_t addr, uint16_t bank, uint8_t bank_size_kb);
void cart_unmap(struct cart *ctx, enum mem type, uint16_t addr);
void cart_map_ciram_offset(struct cart *ctx, uint8_t dest, enum mem type, size_t offset);
void cart_map_ciram_slot(struct cart *ctx, uint8_t dest, uint8_t src);
void cart_map_ciram(struct cart *ctx, NES_Mirror mirror);
void cart_unmap_ciram(struct cart *ctx, uint8_t dest);

// Utility
size_t cart_get_size(struct cart *ctx, enum mem type);
uint8_t *cart_get_mem(struct cart *ctx, enum mem type);
uint16_t cart_get_last_bank(struct cart *ctx, uint16_t bank_size);
enum mem cart_get_chr_type(struct cart *ctx);
void *cart_get_mapper(struct cart *ctx);
const NES_CartDesc *cart_get_desc(struct cart *ctx);

// IO
uint8_t cart_read(struct cart *ctx, enum mem type, uint16_t addr, bool *hit);
void cart_write(struct cart *ctx, enum mem type, uint16_t addr, uint8_t v);
uint8_t cart_prg_read(struct cart *cart, struct apu *apu, uint16_t addr, bool *mem_hit);
void cart_prg_write(struct cart *cart, struct apu *apu, uint16_t addr, uint8_t v);
uint8_t cart_chr_read(struct cart *cart, uint16_t addr, enum mem type, bool nt);

// Hooks
void cart_ppu_a12_toggle(struct cart *cart);
void cart_ppu_write_hook(struct cart *cart, uint16_t addr, uint8_t v);
bool cart_block_2007(struct cart *cart);

// Step
void cart_step(struct cart *cart, struct cpu *cpu, struct apu *apu);

// SRAM
size_t cart_get_sram_size(struct cart *cart);
void *cart_get_sram(struct cart *cart);

// Lifecycle
struct cart *cart_create(const void *rom, size_t rom_size, const NES_CartDesc *desc);
void cart_destroy(struct cart **cart);
void cart_reset(struct cart *cart);

// FDS
struct cart *cart_fds_create(const void *bios, size_t bios_size, const void *disks, size_t disks_size);
bool cart_fds_set_disk(struct cart *cart, int8_t disk);
int8_t cart_fds_get_disk(struct cart *cart);
uint8_t cart_fds_get_num_disks(struct cart *cart);

// State
size_t cart_get_state_size(struct cart *cart);
bool cart_set_state(struct cart *cart, const void *state, size_t size);
bool cart_get_state(struct cart *cart, void *state, size_t size);
