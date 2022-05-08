// https://wiki.nesdev.com/w/index.php/MMC3
// https://wiki.nesdev.com/w/index.php/INES_Mapper_206

struct mmc3 {
	uint8_t REG[8];
	uint8_t prg_mode;
	uint8_t chr_mode;
	uint8_t bank_update;
	bool ram_enable;
	bool ram_read_enable;

	struct {
		uint16_t counter;
		uint8_t period;
		bool enable;
		bool reload;
		bool pending;
		bool ack;
	} irq;
};

static_assert(sizeof(struct mmc3) <= MAPPER_MAX, "Mapper is too big");

static void mmc3_map_prg(struct cart *cart, struct mmc3 *mmc3)
{
	uint16_t b0 = mmc3->REG[6];
	uint16_t b1 = cart_get_last_bank(cart, 0x2000) - 1;

	cart_map(cart, PRG_ROM, 0x8000, mmc3->prg_mode == 0 ? b0 : b1, 8);
	cart_map(cart, PRG_ROM, 0xA000, mmc3->REG[7], 8);
	cart_map(cart, PRG_ROM, 0xC000, mmc3->prg_mode == 0 ? b1 : b0, 8);
}

static void mmc3_map_chr(struct cart *cart, struct mmc3 *mmc3)
{
	enum mem type = cart_get_chr_type(cart);

	uint16_t o1 = mmc3->chr_mode == 0 ? 0 : 0x1000;
	uint16_t o2 = mmc3->chr_mode == 0 ? 4 : 0;

	cart_map(cart, type, o1 + 0x0000, mmc3->REG[0] >> 1, 2);
	cart_map(cart, type, o1 + 0x0800, mmc3->REG[1] >> 1, 2);

	for (uint8_t x = 0; x < 4; x++)
		cart_map(cart, type, (o2 + x) * 0x0400, mmc3->REG[2 + x], 1);
}

static void mmc3_create(struct cart *cart)
{
	struct mmc3 *mmc3 = cart_get_mapper(cart);

	cart_map(cart, PRG_ROM, 0xE000, cart_get_last_bank(cart, 0x2000), 8);
	mmc3->ram_enable = true;
	mmc3->ram_read_enable = true;

	mmc3->REG[6] = 0;
	mmc3->REG[7] = 1;
	mmc3_map_prg(cart, mmc3);

	cart_map(cart, PRG_RAM, 0x6000, 0, 8);
}

static void mmc3_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct mmc3 *mmc3 = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		if (mmc3->ram_enable)
			cart_write(cart, PRG, addr, v);

	} else {
		if (hdr->mapper == 206 && addr > 0x9FFF)
			return;

		switch (addr & 0xE001) {
			case 0x8000:
				mmc3->bank_update = v & 0x07;
				if (hdr->mapper == 4) {
					mmc3->prg_mode = (v & 0x40) >> 6;
					mmc3->chr_mode = (v & 0x80) >> 7;
				}
				mmc3_map_chr(cart, mmc3);
				mmc3_map_prg(cart, mmc3);
				break;
			case 0x8001:
				mmc3->REG[mmc3->bank_update] = (hdr->mapper == 4) ? v : v & 0x3F;

				if (mmc3->bank_update < 6) {
					mmc3_map_chr(cart, mmc3);

				} else {
					mmc3_map_prg(cart, mmc3);
				}
				break;
			case 0xA000:
				if (hdr->mirror != NES_MIRROR_FOUR)
					cart_map_ciram(cart, (v & 0x01) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
				break;
			case 0xA001: // RAM protect, MMC3 uses 8KB of SRAM, MMC6 uses 1KB
				if (cart_get_size(cart, PRG_SRAM) == 0x2000) {
					mmc3->ram_enable = !(v & 0x40);
					mmc3->ram_read_enable = v & 0x80;

				} else {
					NES_Log("MMC6 RAM protect: %x", v);
				}
				break;
			case 0xC000:
				mmc3->irq.period = v;
				break;
			case 0xC001:
				mmc3->irq.reload = true;
				break;
			case 0xE000:
				mmc3->irq.ack = true;
				mmc3->irq.enable = false;
				break;
			case 0xE001:
				mmc3->irq.enable = true;
				break;
			default:
				NES_Log("Uncaught MMC3 write %X: %X", addr, v);
				break;
		}
	}
}

static uint8_t mmc3_prg_read(struct cart *cart, uint16_t addr, bool *mem_hit)
{
	struct mmc3 *mmc3 = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000 && !mmc3->ram_read_enable)
		return 0;

	return cart_read(cart, PRG, addr, mem_hit);
}

static void mmc3_ppu_a12_toggle(struct cart *cart)
{
	struct mmc3 *mmc3 = cart_get_mapper(cart);

	mmc3->irq.pending = true;
}

static void mmc3_step(struct cart *cart, struct cpu *cpu)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct mmc3 *mmc3 = cart_get_mapper(cart);

	if (mmc3->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		mmc3->irq.ack = false;
	}

	if (mmc3->irq.pending) {
		bool set_irq = true;

		if (mmc3->irq.counter == 0 || mmc3->irq.reload) {
			if (hdr->submapper == 4 || hdr->submapper == 1)
				set_irq = mmc3->irq.reload;

			mmc3->irq.reload = false;
			mmc3->irq.counter = mmc3->irq.period;

		} else {
			mmc3->irq.counter--;
		}

		if (set_irq && mmc3->irq.enable && mmc3->irq.counter == 0)
			cpu_irq(cpu, IRQ_MAPPER, true);

		mmc3->irq.pending = false;
	}
}
