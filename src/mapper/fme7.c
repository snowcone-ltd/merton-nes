// https://wiki.nesdev.com/w/index.php/Sunsoft_FME-7

struct fme7 {
	uint8_t REG[2];
	bool ram_enable;

	struct {
		uint16_t value;
		bool enable;
		bool cycle;
		bool ack;
	} irq;

	int32_t vol[32];

	struct fme7_audio {
		bool flip;
		bool disable;
		uint8_t volume;
		uint16_t frequency;
		uint16_t counter;
		uint16_t divider;
	} audio[3];
};

static_assert(sizeof(struct fme7) <= MAPPER_MAX, "Mapper is too big");


// Cart

#define fme7_double_to_u16(v) \
	lrint((v) * 65535.0)

static void fme7_create(struct cart *cart)
{
	struct fme7 *fme7 = cart_get_mapper(cart);

	cart_map(cart, PRG_ROM, 0xE000, cart_get_last_bank(cart, 0x2000), 8);
	cart_map(cart, PRG_RAM, 0x6000, 0, 8);
	cart_map_ciram(cart, NES_MIRROR_VERTICAL);

	for (int32_t x = 0; x < 32; x++)
		fme7->vol[x] = fme7_double_to_u16(x == 0 ? 0.0 : 1.0 / pow(1.6, 1.0 / 2 * (31 - x)));
}

static void fme7_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct fme7 *fme7 = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		if (fme7->ram_enable)
			cart_write(cart, PRG, addr, v);

	// CMD
	} else if (addr >= 0x8000 && addr < 0xA000) {
		fme7->REG[0] = v & 0x0F;

	// Parameter
	} else if (addr >= 0xA000 && addr < 0xC000) {
		switch (fme7->REG[0]) {
			case 0x0: // CHR
			case 0x1:
			case 0x2:
			case 0x3:
			case 0x4:
			case 0x5:
			case 0x6:
			case 0x7:
				cart_map(cart, CHR_ROM, fme7->REG[0] * 0x0400, v, 1);
				break;
			case 0x8: { // PRG0 (RAM or ROM)
				enum mem type = v & 0x40 ? PRG_RAM : PRG_ROM;
				fme7->ram_enable = v & 0x80;
				cart_map(cart, type, 0x6000, v & 0x3F, 8);

				if (type == PRG_RAM && !fme7->ram_enable) { // Unmap address space
					cart_unmap(cart, PRG, 0x6000);
					cart_unmap(cart, PRG, 0x7000);
				}
				break;
			}
			case 0x9: // PRG1-3 (ROM)
			case 0xA:
			case 0xB:
				cart_map(cart, PRG_ROM, 0x8000 + (fme7->REG[0] - 0x9) * 0x2000, v & 0x3F, 8);
				break;
			case 0xC: // Mirroring
				switch (v & 0x03) {
					case 0: cart_map_ciram(cart, NES_MIRROR_VERTICAL);    break;
					case 1: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL);  break;
					case 2: cart_map_ciram(cart, NES_MIRROR_SINGLE0);     break;
					case 3: cart_map_ciram(cart, NES_MIRROR_SINGLE1);     break;
				}
				break;
			case 0xD: // IRQ
				fme7->irq.enable = v & 0x01;
				fme7->irq.cycle = v & 0x80;
				fme7->irq.ack = true;
				break;
			case 0xE:
				fme7->irq.value = (fme7->irq.value & 0xFF00) | v;
				break;
			case 0xF:
				fme7->irq.value = (fme7->irq.value & 0x00FF) | ((uint16_t) v << 8);
				break;
		}

	// Sunsoft 5B Audio CMD
	} else if (addr >= 0xC000 && addr < 0xE000) {
		fme7->REG[1] = v & 0x0F;

	// Sunsoft 5B Audio
	} else if (addr >= 0xE000) {
		switch (fme7->REG[1]) {
			case 0x00: // Channel A, B, C Low Period
			case 0x02:
			case 0x04: {
				struct fme7_audio *c = &fme7->audio[fme7->REG[1] / 2];
				c->frequency = (c->frequency & 0xFF00) | v;
				break;
			}
			case 0x01: // Channel A, B, C High Period
			case 0x03:
			case 0x05: {
				struct fme7_audio *c = &fme7->audio[fme7->REG[1] / 2];
				c->frequency = (c->frequency & 0x00FF) | ((uint16_t) v << 8);
				break;
			}

			case 0x07: // Channel A, B, C Tone Disable
				fme7->audio[0].disable = v & 0x1;
				fme7->audio[1].disable = v & 0x2;
				fme7->audio[2].disable = v & 0x4;
				break;

			case 0x08: // Channel A, B, C, Volume
			case 0x09:
			case 0x0A: {
				struct fme7_audio *c = &fme7->audio[fme7->REG[1] - 0x8];
				c->volume = v & 0xF;
				break;
			}
		}
	}
}


// Audio

static void fme7_channel_step_timer(struct fme7 *fme7, struct apu *apu, uint8_t channel)
{
	struct fme7_audio *c = &fme7->audio[channel];

	if (++c->divider == 16) {
		if (++c->counter >= c->frequency) {
			c->flip = !c->flip;
			c->counter = 0;

			uint8_t output = c->flip && !c->disable ? (c->volume << 1) + (c->volume > 0 ? 1 : 0) : 0;
			apu_set_ext_output(apu, channel, -fme7->vol[output]);
		}

		c->divider = 0;
	}
}


// Step

static void fme7_step(struct cart *cart, struct cpu *cpu, struct apu *apu)
{
	struct fme7 *fme7 = cart_get_mapper(cart);

	// Audio
	for (uint8_t x = 0; x < 3; x++)
		fme7_channel_step_timer(fme7, apu, x);

	// IRQ
	if (fme7->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		fme7->irq.ack = false;
	}

	if (fme7->irq.cycle) {
		if (--fme7->irq.value == 0xFFFF)
			cpu_irq(cpu, IRQ_MAPPER, fme7->irq.enable);
	}
}
