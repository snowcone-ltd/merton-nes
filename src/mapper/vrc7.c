static void vrc7_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct vrc *vrc = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x8000) {
		switch (addr) {
			case 0x8000: // PRG banks
				cart_map(cart, PRG_ROM, 0x8000, v & 0x3F, 8);
				break;
			case 0x8008:
			case 0x8010:
				cart_map(cart, PRG_ROM, 0xA000, v & 0x3F, 8);
				break;
			case 0x9000:
				cart_map(cart, PRG_ROM, 0xC000, v & 0x3F, 8);
				break;
			case 0x9010: // Expansion audio
			case 0x9030:
				break;
			case 0xA000: // CHR banks
			case 0xA008:
			case 0xA010:
			case 0xB000:
			case 0xB008:
			case 0xB010:
			case 0xC000:
			case 0xC008:
			case 0xC010:
			case 0xD000:
			case 0xD008:
			case 0xD010: {
				uint16_t chr_addr = (((addr - 0xA000) / 0x1000) * 2) + ((addr & 0xFF) != 0 ? 1 : 0);
				cart_map(cart, cart_get_chr_type(cart), chr_addr * 0x0400, v, 1);
				break;
			}
			case 0xE000: // Mirroring
				vrc_mirror(cart, v & 0x03);
				break;
			case 0xE008: // IRQ
			case 0xE010:
				vrc->irq.value = v;
				break;
			case 0xF000:
				vrc_set_irq_control(vrc, v);
				break;
			case 0xF008:
			case 0xF010:
				vrc_ack_irq(vrc);
				break;
			default:
				NES_Log("Uncaught VRC7 write %x: %x", addr, v);
		}
	}
}
