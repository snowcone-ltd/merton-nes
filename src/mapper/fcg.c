// https://wiki.nesdev.com/w/index.php/INES_Mapper_016
// https://wiki.nesdev.com/w/index.php/INES_Mapper_159

struct fcg {
	struct {
		uint16_t counter;
		uint16_t value;
		bool enable;
		bool ack;
	} irq;
};

static_assert(sizeof(struct fcg) <= MAPPER_MAX, "Mapper is too big");

static void fcg_create(struct cart *cart)
{
	cart_map(cart, PRG_ROM, 0xC000, cart_get_last_bank(cart, 0x4000), 16);
	cart_map_ciram(cart, NES_MIRROR_VERTICAL);
}

static void fcg_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct fcg *fcg = cart_get_mapper(cart);

	uint16_t addr_low = 0x6000;
	uint16_t addr_high = 0x7FFF;
	uint16_t addr_sub = 0;
	bool alt = (hdr->mapper == 16 && hdr->submapper == 5) || hdr->mapper == 159;

	if (alt) {
		addr_low = 0x8000;
		addr_high = 0xFFFF;
		addr_sub = 0x2000;
	}

	if (addr >= addr_low && addr <= addr_high) {
		addr -= addr_sub;
		addr &= 0xE00F;

		switch (addr) {
			case 0x6000: //CHR
			case 0x6001:
			case 0x6002:
			case 0x6003:
			case 0x6004:
			case 0x6005:
			case 0x6006:
			case 0x6007:
				cart_map(cart, CHR_ROM, (addr - 0x6000) * 0x0400, v, 1);
				break;
			case 0x6008: //PRG
				cart_map(cart, PRG_ROM, 0x8000, v & 0x0F, 16);
				break;
			case 0x6009: //Mirroring
				switch (v & 0x03) {
					case 0: cart_map_ciram(cart, NES_MIRROR_VERTICAL);   break;
					case 1: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL); break;
					case 2: cart_map_ciram(cart, NES_MIRROR_SINGLE0);    break;
					case 3: cart_map_ciram(cart, NES_MIRROR_SINGLE1);    break;
				}
				break;
			case 0x600A: //IRQ
				fcg->irq.enable = v & 0x01;
				fcg->irq.ack = true;

				if (alt)
					fcg->irq.counter = fcg->irq.value;
				break;
			case 0x600B:
				if (alt) {
					fcg->irq.value = (fcg->irq.value & 0xFF00) | v;
				} else {
					fcg->irq.counter = (fcg->irq.counter & 0xFF00) | v;
				}
				break;
			case 0x600C:
				if (alt) {
					fcg->irq.value = (fcg->irq.value & 0x00FF) | ((uint16_t) v << 8);
				} else {
					fcg->irq.counter = (fcg->irq.counter & 0x00FF) | ((uint16_t) v << 8);
				}
				break;
			case 0x600D: //EEPROM write
				break;
			default:
				NES_Log("Uncaught Bandai FCG write %x: %x", addr, v);
		}
	}
}

static void fcg_step(struct cart *cart, struct cpu *cpu)
{
	struct fcg *fcg = cart_get_mapper(cart);

	if (fcg->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		fcg->irq.ack = false;
	}

	if (fcg->irq.enable) {
		if (fcg->irq.counter == 0xFFFE) {
			cpu_irq(cpu, IRQ_MAPPER, true);
			fcg->irq.enable = false;
		} else {
			fcg->irq.counter--;
		}
	}
}
