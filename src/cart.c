#include "cart.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>


// Mappers

#if !defined(_MSC_VER)
	#define static_assert _Static_assert
#endif

#include "mapper/fcg.c"
#include "mapper/fds.c"
#include "mapper/fme7.c"
#include "mapper/jaleco.c"
#include "mapper/mapper.c"
#include "mapper/mmc1.c"
#include "mapper/mmc2.c"
#include "mapper/mmc3.c"
#include "mapper/mmc5.c"
#include "mapper/namco.c"
#include "mapper/vrc.c"
#include "mapper/vrc6.c"
#include "mapper/vrc7.c"


// Cart

#define PRG_SLOT 0x1000
#define CHR_SLOT 0x0400

#define PRG_SHIFT 12
#define CHR_SHIFT 10

struct cart {
	NES_CartDesc hdr;

	// PRG, CHR
	struct range {
		// CHR range uses [SPR, BG] only for MMC5
		// PRG range only uses [0]
		// 16 slots of either 4KB for PRG or 1KB for CHR
		struct map {
			enum mem type;
			struct memory *mem;
			size_t offset;
		} map[2][16];

		// ROM, RAM, EXRAM, CIRAM
		struct memory {
			uint8_t *data;
			size_t size;
		} mem[4];

		uint16_t mask;
		uint8_t shift;
		size_t sram;
		size_t wram;
	} range[2];

	uint8_t *rom;
	size_t rom_size;

	uint8_t *ram;
	size_t ram_size;

	uint8_t mapper[MAPPER_MAX];
};


// Map

#define map_get_range(cart, type) \
	&(cart)->range[(type) >> 2 & 0x1]

#define map_get_slot(range, type, slot) \
	&(range)->map[(type) >> 3 & 0x1][slot]

#define map_get_slot_by_addr(range, type, addr) \
	map_get_slot(range, type, (addr) >> (range)->shift)

#define map_get_mem(range, type) \
	&(range)->mem[(type) & 0x3]

#define map_is_ram(type) \
	((type & 0x3) > 0)

void cart_map(struct cart *ctx, enum mem type, uint16_t addr, uint16_t bank, uint8_t bank_size_kb)
{
	struct range *range = map_get_range(ctx, type);
	struct memory *mem = map_get_mem(range, type);
	if (mem->size == 0)
		return;

	int32_t start_slot = addr >> range->shift;
	int32_t bank_size_bytes = bank_size_kb * 0x0400;
	int32_t bank_offset = bank * bank_size_bytes;
	int32_t end_slot = start_slot + (bank_size_bytes >> range->shift);

	for (int32_t x = start_slot, y = 0; x < end_slot; x++, y++) {
		struct map *m = map_get_slot(range, type, x);

		m->type = type;
		m->mem = mem;
		m->offset = (bank_offset + (y << range->shift)) % mem->size;
	}
}

void cart_unmap(struct cart *ctx, enum mem type, uint16_t addr)
{
	struct range *range = map_get_range(ctx, type);
	struct map *m = map_get_slot_by_addr(range, type, addr);

	memset(m, 0, sizeof(struct map));
}

void cart_map_ciram_offset(struct cart *ctx, uint8_t dest, enum mem type, size_t offset)
{
	struct range *range = map_get_range(ctx, type);
	struct memory *mem = map_get_mem(range, type);
	if (mem->size == 0)
		return;

	range->map[0][dest + 8].type = type;
	range->map[0][dest + 8].mem = mem;
	range->map[0][dest + 8].offset = offset;

	if (dest < 4) {
		range->map[0][dest + 12].type = type;
		range->map[0][dest + 12].mem = mem;
		range->map[0][dest + 12].offset = offset;
	}
}

void cart_map_ciram_slot(struct cart *ctx, uint8_t dest, uint8_t src)
{
	cart_map_ciram_offset(ctx, dest, CIRAM, src * CHR_SLOT);
}

void cart_map_ciram(struct cart *ctx, NES_Mirror mirror)
{
	for (uint8_t x = 0; x < 8; x++)
		cart_map_ciram_slot(ctx, x, (mirror >> (x * 4)) & 0xF);
}

void cart_unmap_ciram(struct cart *ctx, uint8_t dest)
{
	struct range *range = map_get_range(ctx, CHR);

	memset(&range->map[0][dest + 8].mem, 0, sizeof(struct map));

	if (dest < 4)
		memset(&range->map[0][dest + 12].mem, 0, sizeof(struct map));
}


// Utility

size_t cart_get_size(struct cart *ctx, enum mem type)
{
	struct range *range = map_get_range(ctx, type);
	struct memory *mem = map_get_mem(range, type);

	if (type == PRG_SRAM)
		return range->sram;

	return mem->size;
}

uint8_t *cart_get_mem(struct cart *ctx, enum mem type)
{
	struct range *range = map_get_range(ctx, type);
	struct memory *mem = map_get_mem(range, type);

	return mem->data;
}

uint16_t cart_get_last_bank(struct cart *ctx, uint16_t bank_size)
{
	return (uint16_t) (cart_get_size(ctx, PRG_ROM) / bank_size) - 1;
}

enum mem cart_get_chr_type(struct cart *ctx)
{
	return cart_get_size(ctx, CHR_ROM) > 0 ? CHR_ROM : CHR_RAM;
}

void *cart_get_mapper(struct cart *ctx)
{
	return ctx->mapper;
}

const NES_CartDesc *cart_get_desc(struct cart *ctx)
{
	return &ctx->hdr;
}


// IO

uint8_t cart_read(struct cart *ctx, enum mem type, uint16_t addr, bool *hit)
{
	struct range *range = map_get_range(ctx, type);
	struct map *m = map_get_slot_by_addr(range, type, addr);

	if (m->mem) {
		if (hit)
			*hit = true;

		return m->mem->data[m->offset + (addr & range->mask)];
	}

	return 0;
}

void cart_write(struct cart *ctx, enum mem type, uint16_t addr, uint8_t v)
{
	struct range *range = map_get_range(ctx, type);
	struct map *m = map_get_slot_by_addr(range, type, addr);

	if (m->mem && map_is_ram(m->type))
		m->mem->data[m->offset + (addr & range->mask)] = v;
}

uint8_t cart_prg_read(struct cart *cart, struct apu *apu, uint16_t addr, bool *mem_hit)
{
	switch (cart->hdr.mapper) {
		case 4:  return mmc3_prg_read(cart, addr, mem_hit);
		case 5:  return mmc5_prg_read(cart, apu, addr, mem_hit);
		case 19: return namco_prg_read(cart, addr, mem_hit);
		case 20: return fds_prg_read(cart, addr, mem_hit);
		case 21:
		case 22:
		case 23:
		case 25: return vrc_prg_read(cart, addr, mem_hit);
	}

	return cart_read(cart, PRG, addr, mem_hit);
}

void cart_prg_write(struct cart *cart, struct apu *apu, uint16_t addr, uint8_t v)
{
	switch (cart->hdr.mapper) {
		case 1:   mmc1_prg_write(cart, addr, v);       break;
		case 4:
		case 206: mmc3_prg_write(cart, addr, v);       break;
		case 5:   mmc5_prg_write(cart, apu, addr, v);  break;
		case 9:   mmc2_prg_write(cart, addr, v);       break;
		case 10:  mmc2_prg_write(cart, addr, v);       break;
		case 18:  jaleco_prg_write(cart, addr, v);     break;
		case 210:
		case 19:  namco_prg_write(cart, addr, v);      break;
		case 20:  fds_prg_write(cart, addr, v);        break;
		case 21:  vrc_prg_write(cart, addr, v);        break;
		case 22:  vrc_prg_write(cart, addr, v);        break;
		case 24:  vrc6_prg_write(cart, addr, v);       break;
		case 26:  vrc6_prg_write(cart, addr, v);       break;
		case 23:  vrc_prg_write(cart, addr, v);        break;
		case 25:  vrc_prg_write(cart, addr, v);        break;
		case 69:  fme7_prg_write(cart, addr, v);       break;
		case 85:  vrc7_prg_write(cart, addr, v);       break;
		case 16:
		case 159: fcg_prg_write(cart, addr, v);        break;
		case 0:
		case 2:
		case 3:
		case 7:
		case 11:
		case 13:
		case 30:
		case 31:
		case 34:
		case 38:
		case 66:
		case 70:
		case 71:
		case 77:
		case 78:
		case 79:
		case 87:
		case 89:
		case 93:
		case 94:
		case 97:
		case 101:
		case 107:
		case 111:
		case 113:
		case 140:
		case 145:
		case 146:
		case 148:
		case 149:
		case 152:
		case 180:
		case 184:
		case 185: mapper_prg_write(cart, addr, v);     break;
	}
}

uint8_t cart_chr_read(struct cart *cart, uint16_t addr, enum mem type, bool nt)
{
	if (addr < 0x2000) {
		switch (cart->hdr.mapper) {
			case 5:  return mmc5_chr_read(cart, addr, type);
			case 9:
			case 10: return mmc2_chr_read(cart, addr);
		}

	} else if (cart->hdr.mapper == 5) {
		return mmc5_nt_read_hook(cart, addr, type, nt);
	}

	return cart_read(cart, CHR, addr, NULL);
}


// Hooks

void cart_ppu_a12_toggle(struct cart *cart)
{
	if (cart->hdr.mapper == 4)
		mmc3_ppu_a12_toggle(cart);
}

void cart_ppu_write_hook(struct cart *cart, uint16_t addr, uint8_t v)
{
	if (cart->hdr.mapper == 5)
		mmc5_ppu_write_hook(cart, addr, v);
}

bool cart_block_2007(struct cart *cart)
{
	if (cart->hdr.mapper == 185)
		return mapper_block_2007(cart);

	return false;
}


// SRAM

size_t cart_get_sram_size(struct cart *cart)
{
	if (cart->hdr.mapper == 20)
		return cart_get_size(cart, PRG_ROM) - 0x2000;

	if (!cart->hdr.battery)
		return 0;

	return cart_get_size(cart, PRG_SRAM);
}

void *cart_get_sram(struct cart *cart)
{
	if (cart->hdr.mapper == 20)
		return cart_get_mem(cart, PRG_ROM) + 0x2000;

	if (!cart->hdr.battery)
		return NULL;

	return cart_get_mem(cart, PRG_RAM);
}


// Step

void cart_step(struct cart *cart, struct cpu *cpu, struct apu *apu)
{
	switch (cart->hdr.mapper) {
		case 4: mmc3_step(cart, cpu);       break;
		case 5: mmc5_step(cart, cpu);       break;
		case 18: jaleco_step(cart, cpu);    break;
		case 19: namco_step(cart, cpu);     break;
		case 20: fds_step(cart, cpu, apu);  break;
		case 21: vrc_step(cart, cpu);       break;
		case 23: vrc_step(cart, cpu);       break;
		case 24: vrc6_step(cart, cpu, apu); break;
		case 26: vrc6_step(cart, cpu, apu); break;
		case 25: vrc_step(cart, cpu);       break;
		case 69: fme7_step(cart, cpu, apu); break;
		case 85: vrc_step(cart, cpu);       break;
		case 16:
		case 159: fcg_step(cart, cpu);      break;
	}
}


// Lifecycle

#define KB(b) ((b) / 0x0400)

static void cart_set_data_pointers(struct cart *cart)
{
	uint8_t *rom = cart->rom;

	cart->range[RANGE_PRG].mem[MEM_ROM].data = rom;
	rom += cart->range[RANGE_PRG].mem[MEM_ROM].size;

	cart->range[RANGE_CHR].mem[MEM_ROM].data = rom;
	rom += cart->range[RANGE_CHR].mem[MEM_ROM].size;

	uint8_t *ram = cart->ram;

	cart->range[RANGE_PRG].mem[MEM_RAM].data = ram;
	ram += cart->range[RANGE_PRG].mem[MEM_RAM].size;

	cart->range[RANGE_CHR].mem[MEM_RAM].data = ram;
	ram += cart->range[RANGE_CHR].mem[MEM_RAM].size;

	cart->range[RANGE_CHR].mem[MEM_CIRAM].data = ram;
	ram += cart->range[RANGE_CHR].mem[MEM_CIRAM].size;

	cart->range[RANGE_CHR].mem[MEM_EXRAM].data = ram;
	ram += cart->range[RANGE_CHR].mem[MEM_EXRAM].size;
}

static void cart_restore_mem_map(struct cart *cart)
{
	for (uint8_t x = 0; x < 2; x++) {
		for (uint8_t y = 0; y < 16; y++) {
			enum mem prg_type = cart->range[RANGE_PRG].map[x][y].type;
			enum mem chr_type = cart->range[RANGE_CHR].map[x][y].type;

			if (cart->range[RANGE_PRG].map[x][y].mem)
				cart->range[RANGE_PRG].map[x][y].mem = map_get_mem(map_get_range(cart, prg_type), prg_type);

			if (cart->range[RANGE_CHR].map[x][y].mem)
				cart->range[RANGE_CHR].map[x][y].mem = map_get_mem(map_get_range(cart, chr_type), chr_type);
		}
	}
}

static bool cart_init_mapper(struct cart *ctx)
{
	cart_map(ctx, PRG_ROM, 0x8000, 0, 32);
	cart_map(ctx, cart_get_chr_type(ctx), 0x0000, 0, 8);
	cart_map_ciram(ctx, ctx->hdr.mirror);

	switch (ctx->hdr.mapper) {
		case 1:   mmc1_create(ctx);    break;
		case 4:
		case 206: mmc3_create(ctx);    break;
		case 5:   mmc5_create(ctx);    break;
		case 9:   mmc2_create(ctx);    break;
		case 10:  mmc2_create(ctx);    break;
		case 18:  jaleco_create(ctx);  break;
		case 210:
		case 19:  namco_create(ctx);   break;
		case 20:  fds_create(ctx);     break;
		case 21:  vrc2_4_create(ctx);  break;
		case 22:  vrc2_4_create(ctx);  break;
		case 24:  vrc_create(ctx);     break;
		case 26:  vrc_create(ctx);     break;
		case 23:  vrc2_4_create(ctx);  break;
		case 25:  vrc2_4_create(ctx);  break;
		case 69:  fme7_create(ctx);    break;
		case 85:  vrc_create(ctx);     break;
		case 16:
		case 159: fcg_create(ctx);     break;
		case 0:
		case 2:
		case 3:
		case 7:
		case 11:
		case 13:
		case 30:
		case 31:
		case 34:
		case 38:
		case 66:
		case 70:
		case 71:
		case 77:
		case 78:
		case 79:
		case 87:
		case 89:
		case 93:
		case 94:
		case 97:
		case 101:
		case 107:
		case 111:
		case 113:
		case 140:
		case 145:
		case 146:
		case 148:
		case 149:
		case 152:
		case 180:
		case 184:
		case 185: mapper_create(ctx);  break;

		default:
			NES_Log("Mapper %u is unsupported", ctx->hdr.mapper);
			return false;
	}

	return true;
}

static void cart_log_desc(NES_CartDesc *hdr, bool log_ram_sizes)
{
	NES_Log("PRG ROM Size: %uKB", KB(hdr->prgROMSize));
	NES_Log("CHR ROM Size: %uKB", KB(hdr->chrROMSize));

	if (log_ram_sizes) {
		NES_Log("PRG RAM V / NV: %uKB / %uKB", KB(hdr->prgWRAMSize), KB(hdr->prgSRAMSize));
		NES_Log("CHR RAM V / NV: %uKB / %uKB", KB(hdr->chrWRAMSize), KB(hdr->chrSRAMSize));
	}

	NES_Log("Mapper: %u", hdr->mapper);

	if (hdr->submapper != 0)
		NES_Log("Submapper: %x", hdr->submapper);

	NES_Log("Mirroring: %s", hdr->mirror == NES_MIRROR_VERTICAL ? "Vertical" :
		hdr->mirror == NES_MIRROR_HORIZONTAL ? "Horizontal" : "Four Screen");

	NES_Log("Battery: %s", hdr->battery ? "true" : "false");
}

static bool cart_parse_header(const uint8_t *rom, NES_CartDesc *hdr, bool *has_nes2)
{
	if (rom[0] == 'U' && rom[1] == 'N' && rom[2] == 'I' && rom[3] == 'F') {
		NES_Log("UNIF format unsupported");
		return false;
	}

	if (!(rom[0] == 'N' && rom[1] == 'E' && rom[2] == 'S' && rom[3] == 0x1A)) {
		NES_Log("Bad iNES header");
		return false;
	}

	// Archaic iNES
	hdr->offset = 16;
	hdr->prgROMSize = rom[4] * 0x4000;
	hdr->chrROMSize = rom[5] * 0x2000;
	hdr->mirror = (rom[6] & 0x08) ? NES_MIRROR_FOUR : (rom[6] & 0x01) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL;
	hdr->battery = rom[6] & 0x02;
	hdr->offset += (rom[6] & 0x04) ? 512: 0; // Trainer
	hdr->mapper = rom[6] >> 4;

	// Modern iNES
	if ((rom[7] & 0x0C) == 0 && rom[12] == 0 && rom[13] == 0 && rom[14] == 0 && rom[15] == 0) {
		hdr->mapper |= rom[7] & 0xF0;

	// NES 2.0
	} else if (((rom[7] & 0x0C) >> 2) == 0x02) {
		hdr->mapper |= rom[7] & 0xF0;
		hdr->mapper |= (rom[8] & 0x0F) << 8;
		hdr->submapper = rom[8] >> 4;

		uint8_t volatile_shift = rom[10] & 0x0F;
		hdr->prgWRAMSize = volatile_shift ? 64 << volatile_shift : 0;

		uint8_t non_volatile_shift = (rom[10] & 0xF0) >> 4;
		hdr->prgSRAMSize = non_volatile_shift ? 64 << non_volatile_shift : 0;

		volatile_shift = rom[11] & 0x0F;
		hdr->chrWRAMSize = volatile_shift ? 64 << volatile_shift : 0;

		non_volatile_shift = (rom[11] & 0xF0) >> 4;
		hdr->chrSRAMSize = non_volatile_shift ? 64 << non_volatile_shift : 0;

		*has_nes2 = true;
	}

	return true;
}

struct cart *cart_create(const void *rom, size_t rom_size, const NES_CartDesc *desc)
{
	bool r = true;
	struct cart *ctx = calloc(1, sizeof(struct cart));

	bool good_header = false;

	if (desc) {
		ctx->hdr = *desc;
		good_header = true;

	} else {
		if (rom_size < 16) {
			r = false;
			NES_Log("ROM is less than 16 bytes");
			goto except;
		}

		r = cart_parse_header(rom, &ctx->hdr, &good_header);
		if (!r)
			goto except;
	}

	cart_log_desc(&ctx->hdr, good_header);

	ctx->range[RANGE_PRG].mask = PRG_SLOT - 1;
	ctx->range[RANGE_CHR].mask = CHR_SLOT - 1;
	ctx->range[RANGE_PRG].shift = PRG_SHIFT;
	ctx->range[RANGE_CHR].shift = CHR_SHIFT;

	ctx->range[RANGE_PRG].mem[MEM_ROM].size = ctx->hdr.prgROMSize;
	ctx->range[RANGE_CHR].mem[MEM_ROM].size = ctx->hdr.chrROMSize;
	ctx->range[RANGE_CHR].mem[MEM_CIRAM].size = 0x4000;
	ctx->range[RANGE_CHR].mem[MEM_EXRAM].size = 0x400; // MMC5 exram lives with chr since it can be mapped to CIRAM

	ctx->range[RANGE_PRG].wram = ctx->hdr.prgWRAMSize;
	ctx->range[RANGE_PRG].sram = ctx->hdr.prgSRAMSize;
	ctx->range[RANGE_CHR].wram = ctx->hdr.chrWRAMSize;
	ctx->range[RANGE_CHR].sram = ctx->hdr.chrSRAMSize;

	// Defaults to be safe with poor iNES headers
	if (!good_header) {
		ctx->range[RANGE_PRG].sram = 0x2000;
		ctx->range[RANGE_PRG].wram = 0x1E000;
		ctx->range[RANGE_CHR].wram = 0x8000;
	}

	ctx->range[RANGE_PRG].mem[MEM_RAM].size = ctx->range[RANGE_PRG].wram + ctx->range[RANGE_PRG].sram;
	ctx->range[RANGE_CHR].mem[MEM_RAM].size = ctx->range[RANGE_CHR].wram + ctx->range[RANGE_CHR].sram;

	size_t prg_rom_size = cart_get_size(ctx, PRG_ROM);
	size_t prg_ram_size = cart_get_size(ctx, PRG_RAM);
	size_t chr_rom_size = cart_get_size(ctx, CHR_ROM);
	size_t chr_ram_size = cart_get_size(ctx, CHR_RAM);
	size_t ciram_size = cart_get_size(ctx, CIRAM);
	size_t exram_size = cart_get_size(ctx, EXRAM);

	if (ctx->hdr.offset + prg_rom_size > rom_size) {
		r = false;
		NES_Log("PRG ROM size is incorrect");
		goto except;
	}

	if (ctx->hdr.offset + prg_rom_size + chr_rom_size > rom_size) {
		r = false;
		NES_Log("CHR ROM size is incorrect");
		goto except;
	}

	ctx->ram_size = prg_ram_size + chr_ram_size + ciram_size + exram_size;
	ctx->ram = calloc(ctx->ram_size, 1);

	ctx->rom_size = prg_rom_size + chr_rom_size;
	ctx->rom = calloc(ctx->rom_size, 1);

	memcpy(ctx->rom, (uint8_t *) rom + ctx->hdr.offset, prg_rom_size);
	memcpy(ctx->rom + prg_rom_size, (uint8_t *) rom + ctx->hdr.offset + prg_rom_size, chr_rom_size);

	cart_set_data_pointers(ctx);

	r = cart_init_mapper(ctx);
	if (!r)
		goto except;

	except:

	if (!r)
		cart_destroy(&ctx);

	return ctx;
}

void cart_destroy(struct cart **cart)
{
	if (!cart || !*cart)
		return;

	struct cart *ctx = *cart;

	free(ctx->rom);
	free(ctx->ram);

	free(ctx);
	*cart = NULL;
}

void cart_reset(struct cart *cart)
{
	memset(cart->mapper, 0, MAPPER_MAX);
	memset(cart->ram, 0, cart->ram_size);

	cart_init_mapper(cart);
}


// FDS

struct cart *cart_fds_create(const void *bios, size_t bios_size, const void *disks, size_t disks_size)
{
	if (bios_size != 0x2000) {
		NES_Log("BIOS is not 8KB");
		return NULL;
	}

	// Useless FDS header adjustment
	if (!memcmp(disks, "FDS", 3)) {
		disks = (const uint8_t *) disks + 16;
		disks_size -= 16;
	}

	if (fds_side_size(disks_size) == 0) {
		NES_Log("Disks size is not a multiple of 0xFFDC (.fds) or 0x10000 (.qd)");
		return NULL;
	}

	NES_CartDesc desc = {0};
	desc.mapper = 20;
	desc.mirror = NES_MIRROR_HORIZONTAL;
	desc.prgROMSize = 0x2000 + disks_size;
	desc.prgWRAMSize = 0x8000;
	desc.chrWRAMSize = 0x2000;

	uint8_t *full = calloc(desc.prgROMSize, 1);

	memcpy(full, bios, 0x2000);
	memcpy(full + 0x2000, disks, disks_size);

	struct cart *ctx = cart_create(full, desc.prgROMSize, &desc);
	free(full);

	return ctx;
}

bool cart_fds_set_disk(struct cart *cart, int8_t disk)
{
	return fds_set_disk(cart, disk);
}

int8_t cart_fds_get_disk(struct cart *cart)
{
	return fds_get_disk(cart);
}

uint8_t cart_fds_get_num_disks(struct cart *cart)
{
	return fds_get_num_disks(cart);
}


// State

size_t cart_get_state_size(struct cart *cart)
{
	return sizeof(struct cart) + cart->ram_size;
}

bool cart_set_state(struct cart *cart, const void *state, size_t size)
{
	if (size < cart_get_state_size(cart))
		return false;

	uint8_t *rom = cart->rom;

	free(cart->ram);

	*cart = *((const struct cart *) state);
	cart->rom = rom;

	cart->ram = calloc(cart->ram_size, 1);
	memcpy(cart->ram, (uint8_t *) state + sizeof(struct cart), cart->ram_size);

	cart_set_data_pointers(cart);
	cart_restore_mem_map(cart);

	return true;
}

bool cart_get_state(struct cart *cart, void *state, size_t size)
{
	if (size < cart_get_state_size(cart))
		return false;

	memcpy(state, cart, sizeof(struct cart));
	memcpy((uint8_t *) state + sizeof(struct cart), cart->ram, cart->ram_size);

	return true;
}
