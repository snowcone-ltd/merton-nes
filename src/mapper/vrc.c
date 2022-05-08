// https://wiki.nesdev.com/w/index.php/VRC2_and_VRC4
// https://wiki.nesdev.com/w/index.php/VRC_IRQ

enum vrc_type {
	VRC2A = 0x1600,
	VRC2B = 0x1703,
	VRC2C = 0x1903,
	VRC4A = 0x1501,
	VRC4B = 0x1901,
	VRC4C = 0x1502,
	VRC4D = 0x1902,
	VRC4E = 0x1702,
	VRC4F = 0x1701,
};

struct vrc {
	uint16_t type;
	uint8_t prg_mode;
	uint8_t PRG;
	uint8_t REG;
	uint8_t CHR[16];
	uint8_t echo;
	bool is2;

	struct {
		uint16_t counter;
		uint16_t value;
		int16_t scanline;
		bool enable;
		bool reload;
		bool cycle;
		bool ack;
	} irq;

	int32_t vol[31];

	struct vrc6_pulse {
		bool enabled;
		bool mode;
		uint8_t volume;
		uint8_t duty_value;
		uint8_t duty_cycle;
		uint16_t divider;
		uint16_t frequency;
	} pulse[2];

	struct vrc6_saw {
		bool enabled;
		uint8_t clock;
		uint8_t accum_rate;
		uint8_t accumulator;
		uint16_t divider;
		uint16_t frequency;
	} saw;
};

static_assert(sizeof(struct vrc) <= MAPPER_MAX, "Mapper is too big");

#define FIX_ADDR(addr, n)   (((addr) & 0xF000) | (n))
#define MATCH_ADDR(addr, x) (((addr) & (x)) == (x))
#define TEST_ADDR(addr, x)  ((addr) & (x))

#define vrc_double_to_u16(v) \
	lrint((v) * 65535.0)

static void vrc_prg_map(struct cart *cart, struct vrc *vrc)
{
	uint16_t last_bank = cart_get_last_bank(cart, 0x2000);

	if (!vrc->prg_mode) {
		cart_map(cart, PRG_ROM, 0x8000, vrc->PRG, 8);
		cart_map(cart, PRG_ROM, 0xC000, last_bank - 1, 8);

	} else {
		cart_map(cart, PRG_ROM, 0x8000, last_bank - 1, 8);
		cart_map(cart, PRG_ROM, 0xC000, vrc->PRG, 8);
	}
}

static void vrc_create(struct cart *cart)
{
	struct vrc *vrc = cart_get_mapper(cart);

	cart_map(cart, PRG_ROM, 0xE000, cart_get_last_bank(cart, 0x2000), 8);
	cart_map(cart, PRG_RAM, 0x6000, 0, 8);

	vrc->irq.scanline = 341;

	for (int32_t x = 0; x < 31; x++)
		vrc->vol[x] = vrc_double_to_u16(95.52 / (8128.0 / (double) x + 100.0));
}

static void vrc2_4_create(struct cart *cart)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);
	struct vrc *vrc = cart_get_mapper(cart);

	vrc_create(cart);

	vrc->type = (hdr->mapper << 8) | hdr->submapper;
	vrc->is2 = (vrc->type == VRC2A || vrc->type == VRC2B || vrc->type == VRC2C);

	vrc_prg_map(cart, vrc);
}

static uint16_t vrc_repin(uint16_t addr, uint8_t p0, uint8_t p1, uint8_t p2)
{
	if (MATCH_ADDR(addr, p0)) return FIX_ADDR(addr, 3);
	if (MATCH_ADDR(addr, p1)) return FIX_ADDR(addr, 2);
	if (MATCH_ADDR(addr, p2)) return FIX_ADDR(addr, 1);

	return addr & 0xF000;
}

static uint16_t vrc_legacy_repin(uint16_t addr, uint8_t p0a, uint8_t p0b, uint8_t p1, uint8_t p2)
{
	if (MATCH_ADDR(addr, p0a) || MATCH_ADDR(addr, p0b)) return FIX_ADDR(addr, 3);
	if (TEST_ADDR(addr, p1)) return FIX_ADDR(addr, 2);
	if (TEST_ADDR(addr, p2)) return FIX_ADDR(addr, 1);

	return addr & 0xF000;
}

static uint16_t vrc_rejigger_pins(struct cart *cart, struct vrc *vrc, uint16_t addr)
{
	const NES_CartDesc *hdr = cart_get_desc(cart);

	// Precise mapper/submapper selection
	switch (vrc->type) {
		case VRC2A:
		case VRC2C:
		case VRC4B: return vrc_repin(addr, 0x03, 0x01, 0x02);
		case VRC2B:
		case VRC4F: return vrc_repin(addr, 0x03, 0x02, 0x01);
		case VRC4A: return vrc_repin(addr, 0x06, 0x04, 0x02);
		case VRC4C: return vrc_repin(addr, 0xC0, 0x80, 0x40);
		case VRC4D: return vrc_repin(addr, 0x0C, 0x04, 0x08);
		case VRC4E: return vrc_repin(addr, 0x0C, 0x08, 0x04);
	}

	// Legacy emulation
	switch (hdr->mapper) {
		case 23: return vrc_legacy_repin(addr, 0x0C, 0x03, 0x0A, 0x05);
		case 25: return vrc_legacy_repin(addr, 0x0C, 0x03, 0x05, 0x0A);
		case 21:
		default: return vrc_legacy_repin(addr, 0x06, 0xC0, 0x84, 0x42);
	}
}

static void vrc_set_irq_control(struct vrc *vrc, uint8_t v)
{
	vrc->irq.reload = v & 0x01;
	vrc->irq.enable = v & 0x02;
	vrc->irq.cycle = v & 0x04;

	if (vrc->irq.enable) {
		vrc->irq.counter = vrc->irq.value;
		vrc->irq.scanline = 341;
	}

	vrc->irq.ack = true;
}

static void vrc_ack_irq(struct vrc *vrc)
{
	vrc->irq.ack = true;
	vrc->irq.enable = vrc->irq.reload;
}

static void vrc_mirror(struct cart *cart, uint8_t v)
{
	switch (v) {
		case 0: cart_map_ciram(cart, NES_MIRROR_VERTICAL);   break;
		case 1: cart_map_ciram(cart, NES_MIRROR_HORIZONTAL); break;
		case 2: cart_map_ciram(cart, NES_MIRROR_SINGLE0);    break;
		case 3: cart_map_ciram(cart, NES_MIRROR_SINGLE1);    break;
	}
}

static void vrc_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct vrc *vrc = cart_get_mapper(cart);

	if (addr >= 0x6000 && addr < 0x8000) {
		cart_write(cart, PRG, addr, v);
		vrc->echo = v;

	} else if (addr >= 0x8000) {
		addr = vrc_rejigger_pins(cart, vrc, addr);

		switch (addr) {
			case 0x8000: // PRG first/last banks
			case 0x8001:
			case 0x8002:
			case 0x8003:
				vrc->PRG = v & 0x1F;
				vrc_prg_map(cart, vrc);
				break;
			case 0x9000: // Mirroring
			case 0x9001:
				vrc_mirror(cart, v & (vrc->is2 ? 0x01 : 0x03));
				break;
			case 0x9002: // PRG mode
			case 0x9003:
				if (vrc->is2) {
					vrc_prg_write(cart, 0x9000, v);
					return;
				}

				vrc->prg_mode = v & 0x02;
				vrc_prg_map(cart, vrc);
				break;
			case 0xA000: // PRG middle bank
			case 0xA001:
			case 0xA002:
			case 0xA003:
				cart_map(cart, PRG_ROM, 0xA000, v & 0x1F, 8);
				break;
			case 0xB000: // CHR
			case 0xB001:
			case 0xB002:
			case 0xB003:
			case 0xC000:
			case 0xC001:
			case 0xC002:
			case 0xC003:
			case 0xD000:
			case 0xD001:
			case 0xD002:
			case 0xD003:
			case 0xE000:
			case 0xE001:
			case 0xE002:
			case 0xE003: {
				uint8_t slot = (((addr - 0xB000) & 0xF000) >> 12) * 2 + (addr & 0x0003) / 2;

				if (addr % 2 == 0) {
					vrc->CHR[slot] = (vrc->CHR[slot] & 0x01F0) | (v & 0x0F);
				} else {
					vrc->CHR[slot] = (vrc->CHR[slot] & 0x000F) | ((v & 0x1F) << 4);
				}

				uint16_t bank = vrc->type == VRC2A ? vrc->CHR[slot] >> 1 : vrc->CHR[slot];
				cart_map(cart, CHR_ROM, slot * 0x0400, bank, 1);
				break;
			}
			case 0xF000: // IRQ
				vrc->irq.value = (vrc->irq.value & 0xF0) | (v & 0x0F);
				break;
			case 0xF001:
				vrc->irq.value = (vrc->irq.value & 0x0F) | ((v & 0x0F) << 4);
				break;
			case 0xF002:
				vrc_set_irq_control(vrc, v);
				break;
			case 0xF003:
				vrc_ack_irq(vrc);
				break;
			default:
				NES_Log("Uncaught VRC2/4 write %x: %x", addr, v);
		}
	}
}

static uint8_t vrc_prg_read(struct cart *cart, uint16_t addr, bool *hit)
{
	struct vrc *vrc = cart_get_mapper(cart);
	uint8_t v = cart_read(cart, PRG, addr, hit);

	// VRC boards without RAM mapped here must echo the last
	// written value back
	if (!*hit && addr >= 0x6000 && addr < 0x8000) {
		*hit = true;
		return vrc->echo;
	}

	return v;
}

static void vrc_step(struct cart *cart, struct cpu *cpu)
{
	struct vrc *vrc = cart_get_mapper(cart);

	if (vrc->is2)
		return;

	if (vrc->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		vrc->irq.ack = false;
	}

	bool clock = false;

	if (vrc->irq.cycle)
		clock = true;

	if (vrc->irq.scanline <= 0) {
		if (!vrc->irq.cycle)
			clock = true;
		vrc->irq.scanline += 341;
	}
	vrc->irq.scanline -= 3;

	if (vrc->irq.enable && clock) {
		if (vrc->irq.counter == 0xFF) {
			cpu_irq(cpu, IRQ_MAPPER, true);
			vrc->irq.counter = vrc->irq.value;

		} else {
			vrc->irq.counter++;
		}
	}
}
