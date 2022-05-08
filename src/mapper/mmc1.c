// https://wiki.nesdev.com/w/index.php/MMC1
// https://wiki.nesdev.com/w/index.php/SxROM

struct mmc1 {
	uint8_t REG;
	uint8_t PRG;
	uint8_t CHR[2];
	uint8_t chr_mode;
	uint8_t prg_mode;
	uint64_t cycle;
	uint8_t shift;
	uint8_t shift_n;
	enum mem chr_type;
	bool snrom;
	bool sorom;
	bool ram_enable;
};

static_assert(sizeof(struct mmc1) <= MAPPER_MAX, "Mapper is too big");

static void mmc1_map_prg(struct cart *cart, struct mmc1 *mmc1, uint8_t bank)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);

	// SEROM et al
	if (hdr->submapper == 5)
		return;

	// SOROM et al
	uint8_t offset = 0;
	if (mmc1->sorom) {
		// We only consider CHR[0] because in 4KB mode CHR[0] and CHR[1]
		// should be set to the same value, and in 8KB CHR[1] is ignored

		// SUROM, SXROM
		if (cart_get_size(cart, PRG_ROM) == 0x80000 && mmc1->CHR[0] & 0x10)
			offset = 0x10;

		// SOROM, SXROM, but NOT SUROM
		if (cart_get_size(cart, PRG_RAM) > 0x2000)
			cart_map(cart, PRG_RAM, 0x6000, (mmc1->CHR[0] & 0x0C) >> 2, 8);
	}

	switch (mmc1->prg_mode) {
		case 0:
		case 1:
			cart_map(cart, PRG_ROM, 0x8000, (bank + offset) >> 1, 32);
			break;
		case 2:
			cart_map(cart, PRG_ROM, 0x8000, 0 + offset, 16);
			cart_map(cart, PRG_ROM, 0xC000, bank + offset, 16);
			break;
		case 3:
			cart_map(cart, PRG_ROM, 0x8000, bank + offset, 16);
			cart_map(cart, PRG_ROM, 0xC000, 15 + offset, 16);
			break;
	}
}

static void mmc1_map_chr(struct cart *cart, struct mmc1 *mmc1, uint8_t slot, uint8_t bank)
{
	if (mmc1->snrom)
		bank &= 0x01;

	switch (mmc1->chr_mode) {
		case 0:
			// Slot 1 is ignored in 8KB mode
			// Both CHR banks are ignored in 8KB mode in SNROM et al
			if (slot != 1 && !mmc1->snrom)
				cart_map(cart, mmc1->chr_type, 0x0000, bank >> 1, 8);
			break;
		case 1:
			cart_map(cart, mmc1->chr_type, slot * 0x1000, bank, 4);
			break;
	}
}

static void mmc1_update(struct cart *cart, struct mmc1 *mmc1)
{
	mmc1->prg_mode = (mmc1->REG & 0x0C) >> 2;
	mmc1->chr_mode = (mmc1->REG & 0x10) >> 4;

	switch (mmc1->REG & 0x03) {
		case 0: cart_map_ciram(cart, NES_MIRROR_SINGLE0);    break;
		case 1: cart_map_ciram(cart, NES_MIRROR_SINGLE1);    break;
		case 2: cart_map_ciram(cart, NES_MIRROR_VERTICAL);   break;
		case 3: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL); break;
	}

	mmc1->ram_enable = !(mmc1->PRG & 0x10);

	if (mmc1->ram_enable && mmc1->snrom && !mmc1->sorom)
		mmc1->ram_enable = !(mmc1->CHR[0] & 0x10);

	mmc1_map_prg(cart, mmc1, mmc1->PRG & 0x0F);
	mmc1_map_chr(cart, mmc1, 0, mmc1->CHR[0]);
	mmc1_map_chr(cart, mmc1, 1, mmc1->CHR[1]);
}

static void mmc1_create(struct cart *cart)
{
	struct mmc1 *mmc1 = cart_get_mapper(cart);

	// SNROM, also SGROM, SMROM, SOROM, SUROM, SXROM
	mmc1->snrom = cart_get_size(cart, CHR_RAM) == 0x2000 || cart_get_size(cart, CHR_ROM) == 0x2000;

	// SOROM, also SUROM, SXROM (Dragon Warrior 4)
	mmc1->sorom = mmc1->snrom && (cart_get_size(cart, PRG_ROM) == 0x80000 || cart_get_size(cart, PRG_RAM) > 0x2000);

	mmc1->chr_type = cart_get_size(cart, CHR_ROM) > 0 ? CHR_ROM : CHR_RAM;

	mmc1->REG = 0x0C;
	mmc1_update(cart, mmc1);

	cart_map(cart, PRG_RAM, 0x6000, 0, 8);
}

static void mmc1_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct mmc1 *mmc1 = cart_get_mapper(cart);

	// Battery backed sram
	if (addr >= 0x6000 && addr < 0x8000) {
		if (mmc1->ram_enable)
			cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x8000) {
		if (mmc1->cycle == mmc1->cycle - 1)
			return;

		mmc1->cycle = mmc1->cycle;

		// High bit begins the sequence
		if (v & 0x80) {
			mmc1->shift_n = 0;
			mmc1->shift = 0;
			mmc1->REG |= 0x0C;
			mmc1_update(cart, mmc1);

		// If high bit is not set, continue the sequence
		} else {
			mmc1->shift |= (v & 0x01) << mmc1->shift_n;

			if (++mmc1->shift_n == 5) {
				switch (addr & 0xE000) {
					case 0x8000:
						mmc1->REG = mmc1->shift;
						break;
					case 0xA000:
						mmc1->CHR[0] = mmc1->shift;
						break;
					case 0xC000:
						mmc1->CHR[1] = mmc1->shift;
						break;
					case 0xE000:
						mmc1->PRG = mmc1->shift;
						break;
				}

				mmc1_update(cart, mmc1);

				mmc1->shift_n = 0;
				mmc1->shift = 0;
			}
		}
	}
}
