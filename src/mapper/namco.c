// https://wiki.nesdev.com/w/index.php/INES_Mapper_019
// https://wiki.nesdev.com/w/index.php/INES_Mapper_210

struct namco {
	uint8_t REG[8];
	uint8_t CHR[16];
	uint8_t chr_mode;
	bool ram_enable;

	struct {
		uint16_t counter;
		bool enable;
		bool ack;
	} irq;
};

static_assert(sizeof(struct namco) <= MAPPER_MAX, "Mapper is too big");

static void namco_create(struct cart *cart)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct namco *namco = cart_get_mapper(cart);

	cart_map(cart, PRG_ROM, 0xE000, cart_get_last_bank(cart, 0x2000), 8);

	if (hdr->mapper == 210 && hdr->submapper == 1) {
		cart_map(cart, PRG_RAM, 0x6000, 0, 2);
		cart_map(cart, PRG_RAM, 0x6800, 0, 2);
		cart_map(cart, PRG_RAM, 0x7000, 0, 2);
		cart_map(cart, PRG_RAM, 0x7800, 0, 2);

	} else if (hdr->mapper == 19) {
		namco->ram_enable = true;
		cart_map(cart, PRG_RAM, 0x6000, 0, 8);
	}
}

static void namco_map_ppu(struct cart *cart, struct namco *namco)
{
	for (uint8_t x = 0; x < 4; x++) {
		enum mem type = (namco->CHR[x] >= 0xE0 && !(namco->chr_mode & 0x01)) ? CIRAM : CHR_ROM;
		uint8_t v = type == CIRAM ? (namco->CHR[x] & 0x01) : namco->CHR[x];
		cart_map(cart, type, x * 0x0400, v, 1);

		type = (namco->CHR[x + 4] >= 0xE0 && !(namco->chr_mode & 0x02)) ? CIRAM : CHR_ROM;
		v = type == CIRAM ? (namco->CHR[x + 4] & 0x01) : namco->CHR[x + 4];
		cart_map(cart, type, 0x1000 + x * 0x0400, v, 1);

		if (namco->REG[x] >= 0xE0) {
			cart_map_ciram_slot(cart, x, namco->REG[x] & 0x01);
		} else {
			cart_map_ciram_offset(cart, x, CHR_ROM, 0x400 * namco->REG[x]);
		}
	}
}

static void namco_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct namco *namco = cart_get_mapper(cart);

	if (hdr->mapper == 210 && addr < 0x8000)
		return;

	if (addr >= 0x6000 && addr < 0x8000) {
		if (namco->ram_enable)
			cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x4800) {
		switch (addr & 0xF800) {
			case 0x4800: // Expansion audio
				break;
			case 0x5000: // IRQ
				namco->irq.counter = (namco->irq.counter & 0xFF00) | v;
				namco->irq.ack = true;
				break;
			case 0x5800:
				namco->irq.enable = v & 0x80;
				namco->irq.counter = (namco->irq.counter & 0x00FF) | (((uint16_t) v & 0x7F) << 8);
				namco->irq.ack = true;
				break;
			case 0x8000: // CHR
			case 0x8800:
			case 0x9000:
			case 0x9800:
			case 0xA000:
			case 0xA800:
			case 0xB000:
			case 0xB800:
				if (hdr->mapper == 210) {
					cart_map(cart, CHR_ROM, ((addr - 0x8000) / 0x800) * 0x400, v, 1);

				} else {
					namco->CHR[(addr - 0x8000) / 0x800] = v;
					namco_map_ppu(cart, namco);
				}
				break;
			case 0xC000: // Nametables (mapper 19), RAM enable (mapper 210.1)
				if (hdr->mapper == 210 && hdr->submapper == 1)
					namco->ram_enable = v & 0x1;
				// fall-through
			case 0xC800:
			case 0xD000:
			case 0xD800:
				if (hdr->mapper == 19) {
					namco->REG[(addr - 0xC000) / 0x800] = v;
					namco_map_ppu(cart, namco);
				}
				break;
			case 0xE000: // PRG, mirroring (mapper 210.2)
				if (hdr->mapper == 210 && hdr->submapper == 2) {
					switch ((v & 0xC0) >> 6) {
						case 0: cart_map_ciram(cart, NES_MIRROR_SINGLE0);    break;
						case 1: cart_map_ciram(cart, NES_MIRROR_VERTICAL);   break;
						case 2: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL); break;
						case 3: cart_map_ciram(cart, NES_MIRROR_SINGLE1);    break;
					}
				}

				cart_map(cart, PRG_ROM, 0x8000, v & 0x3F, 8);
				break;
			case 0xE800:
				cart_map(cart, PRG_ROM, 0xA000, v & 0x3F, 8);

				if (hdr->mapper == 19) {
					namco->chr_mode = (v & 0xC0) >> 6;
					namco_map_ppu(cart, namco);
				}
				break;
			case 0xF000:
				cart_map(cart, PRG_ROM, 0xC000, v & 0x3F, 8);
				break;
			case 0xF800: // Expansion audio etc.
				break;
			default:
				NES_Log("Uncaught Namco 163/129/175/340 write %x: %x", addr, v);
		}
	}
}

static uint8_t namco_prg_read(struct cart *cart, uint16_t addr, bool *mem_hit)
{
	struct namco *namco = cart_get_mapper(cart);

	*mem_hit = true;

	if (addr >= 0x6000) {
		return cart_read(cart, PRG, addr, mem_hit);

	} else {
		switch (addr & 0xF800) {
			case 0x4800: // Expansion audio
				break;
			case 0x5000: // IRQ
				return namco->irq.counter & 0xFF;
			case 0x5800:
				return namco->irq.counter >> 8;
		}
	}

	*mem_hit = false;

	return 0;
}

static void namco_step(struct cart *cart, struct cpu *cpu)
{
	struct namco *namco = cart_get_mapper(cart);

	if (namco->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		namco->irq.ack = false;
	}

	if (namco->irq.enable) {
		if (++namco->irq.counter == 0x7FFE) {
			cpu_irq(cpu, IRQ_MAPPER, true);
			namco->irq.enable = false;
		}
	}
}
