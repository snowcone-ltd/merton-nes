static void vrc6_map_ppu(struct cart *cart, struct vrc *vrc)
{
	bool reg_mode = vrc->REG & 0x20;
	uint8_t chr_mode = vrc->REG & 0x03;

	for (uint8_t x = 0; x < 8; x++) {
		bool odd_bank = x & 1;
		bool ignore_lsb = chr_mode == 1 || (chr_mode > 1 && x > 3);
		uint8_t mask = reg_mode && ignore_lsb ? 0xFE : 0xFF;
		uint8_t or = reg_mode && odd_bank && ignore_lsb ? 1 : 0;
		uint8_t n = chr_mode == 1 ? x / 2 : chr_mode > 1 && x > 3 ? (x + 4) / 2 : x;

		cart_map(cart, CHR_ROM, x * 0x0400, (vrc->CHR[n] & mask) | or, 1);
	}

	uint8_t c[4] = {0, 0, 0, 0};

	switch (vrc->REG & 0x2F) {
		case 0x20:
		case 0x27:
			c[0] = vrc->CHR[6] & 0xFE;
			c[1] = c[0] + 1;
			c[2] = vrc->CHR[7] & 0xFE;
			c[3] = c[2] + 1;
			break;
		case 0x23:
		case 0x24:
			c[0] = vrc->CHR[6] & 0xFE;
			c[1] = vrc->CHR[7] & 0xFE;
			c[2] = c[0] + 1;
			c[3] = c[1] + 1;
			break;
		case 0x28:
		case 0x2F:
			c[0] = c[1] = vrc->CHR[6] & 0xFE;
			c[2] = c[3] = vrc->CHR[7] & 0xFE;
			break;
		case 0x2B:
		case 0x2C:
			c[0] = c[2] = vrc->CHR[6] | 1;
			c[1] = c[3] = vrc->CHR[7] | 1;
			break;
		default:
			switch (vrc->REG & 0x07) {
				case 0:
				case 6:
				case 7:
					c[0] = c[1] = vrc->CHR[6];
					c[2] = c[3] = vrc->CHR[7];
					break;
				case 1:
				case 5:
					c[0] = vrc->CHR[4];
					c[1] = vrc->CHR[5];
					c[2] = vrc->CHR[6];
					c[3] = vrc->CHR[7];
					break;
				case 2:
				case 3:
				case 4:
					c[0] = c[2] = vrc->CHR[6];
					c[1] = c[3] = vrc->CHR[7];
					break;
			}
	}

	for (uint8_t x = 0; x < 4; x++) {
		if (vrc->REG & 0x10) {
			cart_map_ciram_offset(cart, x, CHR_ROM, 0x400 * c[x]);
		} else {
			cart_map_ciram_slot(cart, x, ~c[x] & 0x01);
		}
	}
}

static void vrc6_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct vrc *vrc = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		cart_write(cart, PRG, addr, v);

	} else if (addr >= 0x8000) {
		if (hdr->mapper == 26)
			addr = (addr & 0xFFFC) | ((addr & 0x01) << 1) | ((addr & 0x02) >> 1);

		switch (addr & 0xF003) {
			// PRG first bank
			case 0x8000:
			case 0x8001:
			case 0x8002:
			case 0x8003:
				cart_map(cart, PRG_ROM, 0x8000, v & 0x0F, 16);
				break;

			// Audio - pulse
			case 0x9000:
			case 0xA000: {
				uint8_t i = (addr >> 12) - 0x9;
				vrc->pulse[i].volume = v & 0x0F;
				vrc->pulse[i].duty_cycle = (v & 0x70) >> 4;
				vrc->pulse[i].mode = v >> 7;
				break;
			}
			case 0x9001:
			case 0xA001: {
				uint8_t i = (addr >> 12) - 0x9;
				vrc->pulse[i].frequency &= 0xFF00;
				vrc->pulse[i].frequency |= v;
				break;
			}
			case 0x9002:
			case 0xA002: {
				uint8_t i = (addr >> 12) - 0x9;
				vrc->pulse[i].frequency &= 0xF0FF;
				vrc->pulse[i].frequency |= (uint16_t) (v & 0x0F) << 8;
				vrc->pulse[i].enabled = v & 0x80;
				break;
			}
			case 0x9003:
				break;

			// PRG second bank
			case 0xC000:
			case 0xC001:
			case 0xC002:
			case 0xC003:
				cart_map(cart, PRG_ROM, 0xC000, v & 0x1F, 8);
				break;

			// Audio - sawtooth
			case 0xB000:
				vrc->saw.accum_rate = v & 0x3F;
				break;
			case 0xB001:
				vrc->saw.frequency &= 0xFF00;
				vrc->saw.frequency |= v;
				break;
			case 0xB002:
				vrc->saw.frequency &= 0xF0FF;
				vrc->saw.frequency |= (uint16_t) (v & 0x0F) << 8;
				vrc->saw.enabled = v & 0x80;
				break;

			// Mirroring, PPU banking
			case 0xB003:
				vrc->REG = v;
				vrc6_map_ppu(cart, vrc);
				break;

			// CHR banking
			case 0xD000:
			case 0xD001:
			case 0xD002:
			case 0xD003:
				vrc->CHR[addr - 0xD000] = v;
				vrc6_map_ppu(cart, vrc);
				break;
			case 0xE000:
			case 0xE001:
			case 0xE002:
			case 0xE003:
				vrc->CHR[4 + (addr - 0xE000)] = v;
				vrc6_map_ppu(cart, vrc);
				break;

			// IRQ control
			case 0xF000:
				vrc->irq.value = v;
				break;
			case 0xF001:
				vrc_set_irq_control(vrc, v);
				break;
			case 0xF002:
				vrc_ack_irq(vrc);
				break;
			default:
				NES_Log("Uncaught VRC6 write %x: %x", addr, v);
		}
	}
}


// Audio

static void vrc6_pulse_step_timer(struct vrc *vrc, struct apu *apu, uint8_t channel)
{
	struct vrc6_pulse *p = &vrc->pulse[channel];

	if (p->divider == 0) {
		p->divider = p->frequency;

		if (p->duty_value == 0) {
			p->duty_value = 15;
		} else {
			p->duty_value--;
		}

		uint8_t output = p->enabled && (p->duty_value <= p->duty_cycle || p->mode) ?
			p->volume : 0;

		apu_set_ext_output(apu, channel, -vrc->vol[output]);

	} else {
		p->divider--;
	}
}

static void vrc6_saw_step_timer(struct vrc *vrc, struct apu *apu)
{
	struct vrc6_saw *s = &vrc->saw;

	if (s->divider == 0) {
		s->divider = s->frequency;

		if (s->clock == 0) {
			s->accumulator = 0;

		} else if ((s->clock & 1) == 0) {
			s->accumulator += s->accum_rate;
			uint8_t output = s->enabled ? (s->accumulator & 0xF8) >> 3 : 0;

			apu_set_ext_output(apu, 2, -vrc->vol[output]);
		}

		if (++s->clock == 14)
			s->clock = 0;

	} else {
		s->divider--;
	}
}


// Step

static void vrc6_step(struct cart *cart, struct cpu *cpu, struct apu *apu)
{
	struct vrc *vrc = cart_get_mapper(cart);

	vrc6_pulse_step_timer(vrc, apu, 0);
	vrc6_pulse_step_timer(vrc, apu, 1);
	vrc6_saw_step_timer(vrc, apu);

	vrc_step(cart, cpu);
}
