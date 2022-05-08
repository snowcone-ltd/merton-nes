// https://wiki.nesdev.com/w/index.php/MMC2
// https://wiki.nesdev.com/w/index.php/MMC4

struct mmc2 {
	uint8_t REG[6];
};

static_assert(sizeof(struct mmc2) <= MAPPER_MAX, "Mapper is too big");

static void mmc2_create(struct cart *cart)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct mmc2 *mmc2 = cart_get_mapper(cart);

	mmc2->REG[4] = 0xFD; // Latch 0
	mmc2->REG[5] = 0xFD; // Latch 1

	if (hdr->mapper == 9) {
		uint16_t last_bank = cart_get_last_bank(cart, 0x2000);

		cart_map(cart, PRG_ROM, 0xA000, last_bank - 2, 8);
		cart_map(cart, PRG_ROM, 0xC000, last_bank - 1, 8);
		cart_map(cart, PRG_ROM, 0xE000, last_bank, 8);

	} else {
		uint16_t last_bank = cart_get_last_bank(cart, 0x4000);

		cart_map(cart, PRG_ROM, 0x8000, 0, 16);
		cart_map(cart, PRG_ROM, 0xC000, last_bank, 16);
		cart_map(cart, PRG_RAM, 0x6000, 0, 8);
	}
}

static uint8_t mmc2_chr_read(struct cart *cart, uint16_t addr)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct mmc2 *mmc2 = cart_get_mapper(cart);

	uint16_t u0 = 0x0FD8;
	uint16_t u1 = 0x0FE8;

	if (hdr->mapper == 10) {
		u0 = 0x0FDF;
		u1 = 0x0FEF;
	}

	// Fetch takes place BEFORE latch is updated
	uint8_t v =  cart_read(cart, CHR, addr, NULL);

	if (addr >= 0x0FD8 && addr <= u0) {
		mmc2->REG[4] = 0xFD;
	} else if (addr >= 0x0FE8 && addr <= u1) {
		mmc2->REG[4] = 0xFE;
	} else if (addr >= 0x1FD8 && addr <= 0x1FDF) {
		mmc2->REG[5] = 0xFD;
	} else if (addr >= 0x1FE8 && addr <= 0x1FEF) {
		mmc2->REG[5] = 0xFE;
	}

	cart_map(cart, CHR_ROM, 0x0000, mmc2->REG[4] == 0xFD ? mmc2->REG[0] : mmc2->REG[1], 4);
	cart_map(cart, CHR_ROM, 0x1000, mmc2->REG[5] == 0xFD ? mmc2->REG[2] : mmc2->REG[3], 4);

	return v;
}

static void mmc2_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct mmc2 *mmc2 = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		if (hdr->mapper == 10)
			cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x8000) {
		switch (addr & 0xF000) {
			case 0xA000:
				cart_map(cart, PRG_ROM, 0x8000, v & 0x0F, hdr->mapper == 10 ? 16 : 8);
				break;
			case 0xB000:
				mmc2->REG[0] = v & 0x1F;
				cart_map(cart, CHR_ROM, 0x0000, mmc2->REG[0], 4);
				break;
			case 0xC000:
				mmc2->REG[1] = v & 0x1F;
				cart_map(cart, CHR_ROM, 0x0000, mmc2->REG[1], 4);
				break;
			case 0xD000:
				mmc2->REG[2] = v & 0x1F;
				cart_map(cart, CHR_ROM, 0x1000, mmc2->REG[2], 4);
				break;
			case 0xE000:
				mmc2->REG[3] = v & 0x1F;
				cart_map(cart, CHR_ROM, 0x1000, mmc2->REG[3], 4);
				break;
			case 0xF000:
				cart_map_ciram(cart, (v & 0x01) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
				break;
		}
	}
}
