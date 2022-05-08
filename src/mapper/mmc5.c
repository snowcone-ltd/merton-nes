// https://wiki.nesdev.com/w/index.php/MMC5

struct mmc5 {
	uint8_t prg_mode;
	uint8_t chr_mode;
	uint8_t exram_mode;
	uint8_t fill_tile;
	uint8_t fill_attr;
	uint8_t exram1;
	uint8_t ram_banks;
	uint16_t multiplicand;
	uint16_t multiplier;
	uint16_t chr_bank_upper;
	uint16_t scanline;
	uint64_t last_ppu_read;
	enum mem active_map;
	bool nt_latch;
	bool exram_latch;
	bool large_sprites;
	bool in_frame;

	struct {
		uint16_t htile;
		uint16_t scroll;
		uint8_t scroll_reload;
		uint8_t tile;
		uint8_t bank;
		bool enable;
		bool right;
		bool fetch;
	} vs;

	struct {
		uint16_t counter;
		uint16_t value;
		int16_t scanline;
		bool enable;
		bool pending;
	} irq;
};

static_assert(sizeof(struct mmc5) <= MAPPER_MAX, "Mapper is too big");

static void mmc5_map_prg16(struct cart *cart, enum mem type, uint16_t addr, uint16_t bank)
{
	cart_map(cart, type, addr, bank & 0xFE, 8);
	cart_map(cart, type, addr + 0x2000, (bank & 0xFE) + 1, 8);
}

static void mmc5_map_prg32(struct cart *cart, enum mem type, uint16_t addr, uint16_t bank)
{
	cart_map(cart, type, addr, bank & 0xFC, 8);
	cart_map(cart, type, addr + 0x2000, (bank & 0xFC) + 1, 8);
	cart_map(cart, type, addr + 0x4000, (bank & 0xFC) + 2, 8);
	cart_map(cart, type, addr + 0x6000, (bank & 0xFC) + 3, 8);
}

static void mmc5_map_prg(struct cart *cart, struct mmc5 *mmc5, int32_t slot, uint16_t bank, enum mem type)
{
	if (slot == 0)
		type = PRG_RAM;

	if (type == PRG_RAM)
		bank = (mmc5->ram_banks > 1 ? (bank & 0x3) : 0) + ((bank & 0x4) >> 2) * mmc5->ram_banks;

	if (slot == 0) {
		cart_map(cart, PRG_RAM, 0x6000, bank, 8);

	} else {
		switch (mmc5->prg_mode) {
			case 0:
				if (slot == 4)
					mmc5_map_prg32(cart, type, 0x8000, bank);
				break;
			case 1:
				if (slot == 2) {
					mmc5_map_prg16(cart, type, 0x8000, bank);
				} else if (slot == 4) {
					mmc5_map_prg16(cart, type, 0xC000, bank);
				}
				break;
			case 2:
				if (slot == 2) {
					mmc5_map_prg16(cart, type, 0x8000, bank);

				} else if (slot > 2) {
					cart_map(cart, type, 0x6000 + (uint16_t) (slot * 0x2000), bank, 8);
				}
				break;
			case 3:
				cart_map(cart, type, 0x6000 + (uint16_t) (slot * 0x2000), bank, 8);
				break;
		}
	}
}

static void mmc5_map_chr(struct cart *cart, struct mmc5 *mmc5, int32_t slot, uint16_t bank, enum mem type)
{
	bank |= mmc5->chr_bank_upper;
	uint8_t mslot = cart_get_size(cart, CHR_ROM) == 0 ? MEM_RAM : MEM_ROM;

	switch (mmc5->chr_mode) {
		case 0:
			cart_map(cart, type | mslot, 0x0000, bank, 8);
			break;
		case 1:
			cart_map(cart, type | mslot, slot == 3 ? 0x0000 : 0x1000, bank, 4);
			break;
		case 3:
			cart_map(cart, type | mslot, (uint16_t) (slot * 0x0400), bank, 1);
			break;
		default:
			NES_Log("Unsupported CHR mode %x", mmc5->chr_mode);
	}
}

static void mmc5_create(struct cart *cart)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);

	mmc5_map_prg16(cart, PRG_ROM, 0xC000, 0xFF);

	mmc5->prg_mode = 3;
	mmc5->active_map = CHR_SPR;

	uint8_t mslot = cart_get_size(cart, CHR_ROM) == 0 ? MEM_RAM : MEM_ROM;
	cart_map(cart, CHR_SPR | mslot, 0x0000, 0, 8);
	cart_map(cart, CHR_BG | mslot, 0x0000, 0, 8);

	if (cart_get_size(cart, PRG_RAM) > 0)
		cart_map(cart, PRG_RAM, 0x6000, 0, 8);

	mmc5->ram_banks = cart_get_size(cart, PRG_RAM) <= 0x4000 ? 1 : 4;
}

static void mmc5_prg_write(struct cart *cart, struct apu *apu, uint16_t addr, uint8_t v)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);

	if (addr >= 0x5C00 && addr < 0x6000) {
		uint8_t *exram = cart_get_mem(cart, EXRAM);
		exram[addr - 0x5C00] = v;

	} else if (addr < 0x6000) {
		switch (addr) {
			case 0x5000: // MMC5 audio pulse, status
			case 0x5002:
			case 0x5003:
			case 0x5004:
			case 0x5006:
			case 0x5007:
			case 0x5015:
				apu_write(apu, NULL, addr - 0x1000, v, true);
				break;
			case 0x5001: // MMC5 audio unused pulse sweep
			case 0x5005:
				break;
			case 0x5010: // MMC5 audio PCM
			case 0x5011:
				break;
			case 0x5100: // PRG mode
				mmc5->prg_mode = v & 0x03;
				break;
			case 0x5101: // CHR mode
				mmc5->chr_mode = v & 0x03;
				break;
			case 0x5102: // PRG RAM protect
			case 0x5103:
				break;
			case 0x5104: // EXRAM mode
				mmc5->exram_mode = v & 0x03;
				break;
			case 0x5105: // Mirroring mode
				for (uint8_t x = 0; x < 4; x++) {
					switch ((v >> (x * 2)) & 0x03) {
						case 0: cart_map_ciram_slot(cart, x, 0);          break;
						case 1: cart_map_ciram_slot(cart, x, 1);          break;
						case 2: cart_map_ciram_offset(cart, x, EXRAM, 0); break;
						case 3: cart_unmap_ciram(cart, x);                break;
					}
				}
				break;
			case 0x5106: // Fill mode tile
				mmc5->fill_tile = v;
				break;
			case 0x5107: // Fill mode color
				mmc5->fill_attr = v & 0x03;
				mmc5->fill_attr |= mmc5->fill_attr << 2;
				mmc5->fill_attr |= mmc5->fill_attr << 4;
				break;
			case 0x5113: // PRG bankswitch
			case 0x5114:
			case 0x5115:
			case 0x5116:
			case 0x5117: {
				bool ram = !(v & 0x80) && (addr == 0x5114 || addr == 0x5115 || addr == 0x5116);
				mmc5_map_prg(cart, mmc5, addr - 0x5113, v & 0x7F, ram ? PRG_RAM : PRG_ROM);
				break;
			}
			case 0x5120: // CHR bankswitch
			case 0x5121:
			case 0x5122:
			case 0x5123:
			case 0x5124:
			case 0x5125:
			case 0x5126:
			case 0x5127:
				mmc5->active_map = CHR_SPR;
				mmc5_map_chr(cart, mmc5, addr - 0x5120, v, CHR_SPR);
				break;
			case 0x5128:
			case 0x5129:
			case 0x512A:
			case 0x512B:
				mmc5->active_map = CHR_BG;
				mmc5_map_chr(cart, mmc5, addr - 0x5128, v, CHR_BG);
				mmc5_map_chr(cart, mmc5, (addr - 0x5128) + 4, v, CHR_BG);
				break;
			case 0x5130:
				mmc5->chr_bank_upper = (uint16_t) (v & 0x03) << 8;
				break;
			case 0x5200: // Vertical split mode
				mmc5->vs.enable = v & 0x80;
				mmc5->vs.right = v & 0x40;
				mmc5->vs.tile = v & 0x1F;
				break;
			case 0x5201:
				mmc5->vs.scroll_reload = v;
				break;
			case 0x5202:
				mmc5->vs.bank = v;
				break;
			case 0x5203: // IRQ line number
				mmc5->irq.scanline = v;
				break;
			case 0x5204: // IRQ enable
				mmc5->irq.enable = v & 0x80;
				break;
			case 0x5205: // Math
				mmc5->multiplicand = v;
				break;
			case 0x5206:
				mmc5->multiplier = v;
				break;
			case 0x5800: // Just Breed unknown
				break;
			default:
				NES_Log("Uncaught MMC5 write %x", addr);
		}

	} else {
		cart_write(cart, PRG, addr, v);
	}
}

static uint8_t mmc5_prg_read(struct cart *cart, struct apu *apu, uint16_t addr, bool *mem_hit)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);

	*mem_hit = true;

	if (addr >= 0x6000) {
		return cart_read(cart, PRG, addr, mem_hit);

	} else if (addr >= 0x5C00 && addr < 0x6000) {
		uint8_t *exram = cart_get_mem(cart, EXRAM);
		return exram[addr - 0x5C00];

	} else {
		switch (addr) {
			case 0x5000: // MMC5 audio pulse
			case 0x5002:
			case 0x5003:
			case 0x5004:
			case 0x5006:
			case 0x5007:
				break;
			case 0x5001: // MMC5 audio unused pulse sweep
			case 0x5005:
				break;
			case 0x5015: // MMC5 audio status
				return apu_read_status(apu, true);
			case 0x5010: // MMC5 audio PCM
			case 0x5011:
				break;
			case 0x5113: // PRG bankswitch
			case 0x5114:
			case 0x5115:
			case 0x5116:
			case 0x5117:
			case 0x5120: // CHR bankswitch
			case 0x5121:
			case 0x5122:
			case 0x5123:
			case 0x5124:
			case 0x5125:
			case 0x5126:
			case 0x5127:
			case 0x5128:
			case 0x5129:
			case 0x512A:
			case 0x512B:
				break;
			case 0x5204: {
				uint8_t r = 0;

				if (mmc5->in_frame) r |= 0x40;
				if (mmc5->irq.pending) r |= 0x80;

				mmc5->irq.pending = false;

				return r;
			}
			case 0x5205:
				return (uint8_t) (mmc5->multiplier * mmc5->multiplicand);
			case 0x5206:
				return (mmc5->multiplier * mmc5->multiplicand) >> 8;
			default:
				NES_Log("Uncaught MMC5 read %x", addr);
				break;
		}
	}

	*mem_hit = false;

	return 0;
}

static void mmc5_scanline(struct mmc5 *mmc5, uint16_t addr)
{
	// Should be true on the first attribute byte fetch of the scanline (cycle 3)
	if (mmc5->irq.counter == 2) {
		if (!mmc5->in_frame) {
			mmc5->in_frame = true;
			mmc5->scanline = 0;
		} else {
			mmc5->scanline++;
		}

		mmc5->irq.pending = mmc5->irq.scanline == mmc5->scanline && mmc5->irq.scanline != 0;

		mmc5->vs.scroll++;

		if (mmc5->scanline == 0)
			mmc5->vs.scroll = mmc5->vs.scroll_reload;

		mmc5->irq.counter = 0;
		mmc5->irq.value = 0xFFFF;
	}

	if (addr == mmc5->irq.value)
		mmc5->irq.counter++;

	mmc5->irq.value = addr;
}

static uint8_t mmc5_nt_read_hook(struct cart *cart, uint16_t addr, enum mem type, bool nt)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);
	uint8_t *exram = cart_get_mem(cart, EXRAM);

	mmc5->last_ppu_read = 0;

	mmc5_scanline(mmc5, addr);

	if (type == CHR_BG) {
		if (nt) {
			mmc5->exram_latch = false;
			mmc5->nt_latch = false;
			mmc5->vs.htile++;

			if (mmc5->vs.htile > 34)
				mmc5->vs.htile = 1;
		}

		uint16_t htile = mmc5->vs.htile >= 32 ? mmc5->vs.htile - 32 : mmc5->vs.htile + 1;
		bool vs_in_range = mmc5->vs.right ? htile >= mmc5->vs.tile : htile < mmc5->vs.tile;

		mmc5->vs.fetch =
			vs_in_range &&
			mmc5->vs.enable &&
			mmc5->exram_mode <= 1;

		if (mmc5->vs.fetch) {
			uint16_t vtile = mmc5->vs.scroll / 8;

			if (vtile >= 30)
				vtile -= 30;

			if (!mmc5->exram_latch) {
				mmc5->exram_latch = true;
				return exram[vtile * 32 + htile];

			} else {
				mmc5->exram_latch = false;
				return exram[0x03C0 + vtile / 32 + htile / 4];
			}

		} else if (mmc5->exram_mode == 1) {
			if (!mmc5->exram_latch) {
				mmc5->exram_latch = true;
				mmc5->exram1 = exram[addr % 0x0400];

			} else {
				mmc5->exram_latch = false;

				uint8_t exattr = (mmc5->exram1 & 0xC0) >> 6;
				exattr |= exattr << 2;
				exattr |= exattr << 4;

				return exattr;
			}
		}
	}

	bool hit = false;
	uint8_t v = cart_read(cart, CHR, addr, &hit);

	if (!hit) { // Unmapped falls through to fill mode
		v = !mmc5->nt_latch ? mmc5->fill_tile : mmc5->fill_attr;
		mmc5->nt_latch = true;
	}

	return v;
}

static void mmc5_ppu_write_hook(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);

	switch (addr) {
		case 0x2000: // PPUCTRL
			mmc5->large_sprites = v & 0x20;
			break;
	}
}

static uint8_t mmc5_chr_read(struct cart *cart, uint16_t addr, enum mem type)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);
	uint8_t *chr_rom = cart_get_mem(cart, CHR_ROM);

	mmc5->last_ppu_read = 0;

	if (mmc5->exram_mode != 1 && !mmc5->large_sprites)
		type = CHR_SPR;

	switch (type) {
		case CHR_BG:
			if (mmc5->vs.fetch) {
				uint16_t fine_y = mmc5->vs.scroll & 0x07;
				return chr_rom[(mmc5->vs.bank * 0x1000 + (addr & 0x0FF8) + fine_y) % cart_get_size(cart, CHR_ROM)];

			} else if (mmc5->exram_mode == 1) {
				uint16_t exbank = (mmc5->chr_bank_upper >> 2) | (mmc5->exram1 & 0x3F);
				return chr_rom[(exbank * 0x1000 + (addr & 0x0FFF)) % cart_get_size(cart, CHR_ROM)];
			}

			return cart_read(cart, CHR_BG, addr, NULL);
		case CHR_SPR:
			return cart_read(cart, CHR_SPR, addr, NULL);
		case CHR_ROM:
			return cart_read(cart, mmc5->active_map, addr, NULL);
		default:
			break;
	}

	return 0;
}

static void mmc5_step(struct cart *cart, struct cpu *cpu)
{
	struct mmc5 *mmc5 = cart_get_mapper(cart);

	if (++mmc5->last_ppu_read >= 3)
		mmc5->in_frame = false;

	cpu_irq(cpu, IRQ_MAPPER, mmc5->irq.pending && mmc5->irq.enable && mmc5->scanline != 0);
}
