// https://wiki.nesdev.com/w/index.php/INES_Mapper_018

struct jaleco {
	uint8_t REG[8];
	uint8_t PRG[8];
	uint8_t CHR[16];
	bool ram_enable;

	struct {
		uint16_t counter;
		uint16_t value;
		bool enable;
		bool ack;
	} irq;
};

static_assert(sizeof(struct jaleco) <= MAPPER_MAX, "Mapper is too big");

static void jaleco_create(struct cart *cart)
{
	struct jaleco *jaleco = cart_get_mapper(cart);

	jaleco->ram_enable = true;

	cart_map(cart, PRG_ROM, 0xE000, cart_get_last_bank(cart, 0x2000), 8);
	cart_map(cart, PRG_RAM, 0x6000, 0, 8);
}

static void jaleco_map_prg(struct cart *cart, struct jaleco *jaleco, uint8_t n, uint8_t v)
{
	jaleco->PRG[n] = v;

	cart_map(cart, PRG_ROM, 0x8000 + (n / 2) * 0x2000,
		(jaleco->PRG[n & 0xE] & 0xF) | ((jaleco->PRG[(n & 0xE) + 1] & 0x3) << 4), 8);
}

static void jaleco_map_chr(struct cart *cart, struct jaleco *jaleco, uint8_t n, uint8_t v)
{
	jaleco->CHR[n] = v;

	cart_map(cart, CHR_ROM, 0x400 * (n / 2),
		(jaleco->CHR[n & 0xE] & 0xF) | ((jaleco->CHR[(n & 0xE) + 1] & 0xF) << 4), 1);
}

static void jaleco_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct jaleco *jaleco = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		if (jaleco->ram_enable)
			cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x8000) {
		switch (addr & 0xF003) {
			case 0x8000: jaleco_map_prg(cart, jaleco, 0, v); break;
			case 0x8001: jaleco_map_prg(cart, jaleco, 1, v); break;
			case 0x8002: jaleco_map_prg(cart, jaleco, 2, v); break;
			case 0x8003: jaleco_map_prg(cart, jaleco, 3, v); break;
			case 0x9000: jaleco_map_prg(cart, jaleco, 4, v); break;
			case 0x9001: jaleco_map_prg(cart, jaleco, 5, v); break;

			case 0x9002:
				jaleco->ram_enable = v & 0x3;
				break;

			case 0xA000: jaleco_map_chr(cart, jaleco, 0, v); break;
			case 0xA001: jaleco_map_chr(cart, jaleco, 1, v); break;
			case 0xA002: jaleco_map_chr(cart, jaleco, 2, v); break;
			case 0xA003: jaleco_map_chr(cart, jaleco, 3, v); break;
			case 0xB000: jaleco_map_chr(cart, jaleco, 4, v); break;
			case 0xB001: jaleco_map_chr(cart, jaleco, 5, v); break;
			case 0xB002: jaleco_map_chr(cart, jaleco, 6, v); break;
			case 0xB003: jaleco_map_chr(cart, jaleco, 7, v); break;
			case 0xC000: jaleco_map_chr(cart, jaleco, 8, v); break;
			case 0xC001: jaleco_map_chr(cart, jaleco, 9, v); break;
			case 0xC002: jaleco_map_chr(cart, jaleco, 10, v); break;
			case 0xC003: jaleco_map_chr(cart, jaleco, 11, v); break;
			case 0xD000: jaleco_map_chr(cart, jaleco, 12, v); break;
			case 0xD001: jaleco_map_chr(cart, jaleco, 13, v); break;
			case 0xD002: jaleco_map_chr(cart, jaleco, 14, v); break;
			case 0xD003: jaleco_map_chr(cart, jaleco, 15, v); break;

			case 0xE000:
			case 0xE001:
			case 0xE002:
			case 0xE003:
				jaleco->REG[addr & 0x3] = v;
				break;

			case 0xF000:
				jaleco->irq.ack = true;
				jaleco->irq.counter = jaleco->REG[0] | (jaleco->REG[1] << 4) | (jaleco->REG[2] << 8) |
					(jaleco->REG[3] << 12);
				break;

			case 0xF001:
				jaleco->irq.ack = true;
				jaleco->irq.enable = v & 0x1;
				jaleco->irq.value = (v & 0x8) ? 0xF : (v & 0x4) ? 0xFF : (v & 0x2) ? 0xFFF : 0xFFFF;
				break;

			case 0xF002:
				switch (v & 0x3) {
					case 0: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL); break;
					case 1: cart_map_ciram(cart, NES_MIRROR_VERTICAL);   break;
					case 2: cart_map_ciram(cart, NES_MIRROR_SINGLE0);    break;
					case 3: cart_map_ciram(cart, NES_MIRROR_SINGLE1);    break;
				}
				break;
		}
	}
}

static void jaleco_step(struct cart *cart, struct cpu *cpu)
{
	struct jaleco *jaleco = cart_get_mapper(cart);

	if (jaleco->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		jaleco->irq.ack = false;
	}

	if (jaleco->irq.enable) {
		uint16_t counter = jaleco->irq.counter & jaleco->irq.value;

		if (--counter == 0)
			cpu_irq(cpu, IRQ_MAPPER, true);

		jaleco->irq.counter = (jaleco->irq.counter & ~jaleco->irq.value) | (counter & jaleco->irq.value);
	}
}
