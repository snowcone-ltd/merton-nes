// https://wiki.nesdev.com/w/index.php/Family_Computer_Disk_System

struct fds {
	struct fds_audio {
		struct fds_env {
			uint32_t timer;
			uint16_t freq;
			uint8_t speed;
			uint8_t gain;
			bool increase;
			bool enable;
		} vol, mod;

		uint16_t overflow;
		uint16_t woverflow;
		uint8_t wtable[64];
		uint8_t mtable[64];
		uint8_t mpos;
		uint8_t wpos;
		uint8_t mvol;
		uint8_t mspeed;
		int8_t counter;
		bool wt_write;
		bool halt_env;
		bool halt_waveform;
		bool mod_halt;

		float prev_x;
		float prev_y;
		int32_t output;
	} audio;

	struct fds_file {
		size_t pos;
		size_t offset;
		size_t block;
		size_t gap;
		size_t data_len;
		uint16_t crc;
		bool eof;
		bool zero;
	} file;

	struct {
		uint16_t counter;
		uint16_t value;
		bool enable;
		bool reload;
		bool pending;
		bool ack;
	} irq;

	int8_t disk;
	int8_t inserted;
	uint8_t num_disks;
	uint8_t ext;
	uint8_t read;
	uint8_t write;
	uint8_t mode;
	uint32_t delay;
	size_t side_size;
	bool crc_ctrl;
	bool motor;
	bool reset;
	bool transfer;
	bool disk_ready;
	bool disk_enable;
	bool sound_enable;
	bool transfer_irq;
	bool ack_transfer;
	bool start;
};

static_assert(sizeof(struct fds) <= MAPPER_MAX, "Mapper is too big");


// Entry

static size_t fds_side_size(size_t disks_size)
{
	return
		disks_size % 0xFFDC  == 0 ? 0xFFDC  : // .fds
		disks_size % 0x10000 == 0 ? 0x10000 : // .qd
		0;
}

static void fds_create(struct cart *cart)
{
	struct fds *fds = cart_get_mapper(cart);

	cart_map(cart, PRG_ROM, 0xE000, 0, 8);
	cart_map(cart, PRG_RAM, 0x6000, 0, 32);
	cart_map(cart, CHR_RAM, 0x0000, 0, 8);

	fds->disk = -1;
	fds->inserted = 0;
	fds->audio.mspeed = 0xFF;

	size_t disks_size = cart_get_size(cart, PRG_ROM) - 0x2000;
	fds->side_size = fds_side_size(disks_size);
	fds->num_disks = (uint8_t) (disks_size / fds->side_size);
}

static bool fds_set_disk(struct cart *cart, int8_t disk)
{
	struct fds *fds = cart_get_mapper(cart);

	if (disk >= fds->num_disks)
		return false;

	fds->inserted = disk;

	return true;
}

static int8_t fds_get_disk(struct cart *cart)
{
	struct fds *fds = cart_get_mapper(cart);

	return fds->disk;
}

static uint8_t fds_get_num_disks(struct cart *cart)
{
	struct fds *fds = cart_get_mapper(cart);

	return fds->num_disks;
}


// Audio

// https://wiki.nesdev.com/w/index.php/FDS_audio

#define LP_A  0.0034983233590242f
#define LP_B -0.9930033532819522f

static const int8_t FDS_MOD_TABLE[] = {0, 1, 2, 4, 5, -4, -2, -1};
static const uint8_t FDS_VOL_TABLE[] = {36, 24, 17, 14};

static void fds_env_reset_timer(struct fds_audio *fds, struct fds_env *env)
{
	env->timer = 8 * (env->speed + 1) * fds->mspeed;
}

static void fds_update_counter(struct fds_audio *fds, int8_t v)
{
	fds->counter = v;

	if (fds->counter >= 64) {
		fds->counter -= 128;

	} else if (fds->counter < -64) {
		fds->counter += 128;
	}
}

static void fds_mod_step(struct fds_audio *fds)
{
	bool enabled = !fds->mod_halt && fds->mod.freq > 0;

	if (enabled) {
		fds->overflow += fds->mod.freq;

		if (fds->overflow < fds->mod.freq) {
			int8_t inc = FDS_MOD_TABLE[fds->mtable[fds->mpos]];
			fds_update_counter(fds, inc == 5 ? 0 : fds->counter + inc);
			fds->mpos = (fds->mpos + 1) & 0x3F;
		}
	}
}

static void fds_env_step(struct fds_audio *fds, struct fds_env *env)
{
	if (env->enable && fds->mspeed > 0) {
		if (--env->timer == 0) {
			fds_env_reset_timer(fds, env);

			if (env->increase && env->gain < 32) {
				env->gain++;

			} else if (!env->increase && env->gain > 0) {
				env->gain--;
			}
		}
	}
}

static int32_t fds_mod_output(struct fds_audio *fds, uint16_t pitch)
{
	int32_t temp = fds->counter * fds->mod.gain;
	int32_t remainder = temp & 0xF;
	temp >>= 4;

	if (remainder > 0 && !(temp & 0x80))
		temp += fds->counter < 0 ? -1 : 2;

	if (temp >= 192) {
		temp -= 256;

	} else if (temp < -64) {
		temp += 256;
	}

	temp *= pitch;
	remainder = temp & 0x3F;
	temp >>= 6;

	if (remainder >= 32)
		temp += 1;

	return temp;
}

static void fds_step_wavetable(struct fds_audio *fds)
{
	bool mod_enabled = !fds->mod_halt && fds->mod.freq > 0;
	int32_t mod_output = mod_enabled ? fds_mod_output(fds, fds->vol.freq) : 0;
	int32_t pitch = fds->vol.freq + mod_output;

	if (pitch > 0 && !fds->wt_write) {
		fds->woverflow += (uint16_t) pitch;

		if (fds->woverflow < pitch)
			fds->wpos = (fds->wpos + 1) & 0x3F;
	}
}

static void fds_step_audio(struct fds_audio *fds, struct apu *apu)
{
	if (!fds->halt_waveform && !fds->halt_env) {
		fds_env_step(fds, &fds->vol);
		fds_env_step(fds, &fds->mod);
	}

	fds_mod_step(fds);

	if (fds->halt_waveform) {
		fds->wpos = 0;

	} else {
		fds_step_wavetable(fds);
	}

	uint8_t gain = fds->vol.gain;
	if (gain > 32)
		gain = 32;

	float output = fds->wtable[fds->wpos] * gain * FDS_VOL_TABLE[fds->mvol] / 2.734f;

	// Low pass
	float y = LP_A * output + LP_A * fds->prev_x - LP_B * fds->prev_y;
	fds->prev_x = output;
	fds->prev_y = y;

	apu_set_ext_output(apu, 0, lrint(fds->prev_y));
}


// IO

static void fds_prg_write(struct cart *cart, uint16_t addr, uint8_t v)
{
	struct fds *fds = cart_get_mapper(cart);

	// Registers
	if (addr <= 0x4026) {
		switch (addr) {
			// IRQ
			case 0x4020:
				fds->irq.value = (fds->irq.value & 0xFF00) | v;
				break;
			case 0x4021:
				fds->irq.value = (fds->irq.value & 0x00FF) | ((uint16_t) v) << 8;
				break;
			case 0x4022:
				fds->irq.reload = v & 0x01;
				fds->irq.enable = (v & 0x02) && fds->disk_enable;

				if (fds->irq.enable) {
					fds->irq.counter = fds->irq.value;

				} else {
					fds->irq.ack = true;
				}
				break;

			// Master I/O enable
			case 0x4023:
				fds->disk_enable = v & 0x01;
				fds->sound_enable = v & 0x02;

				if (!fds->disk_enable) {
					fds->irq.enable = false;
					fds->irq.ack = true;
					fds->ack_transfer = true;
				}
				break;

			// Write data
			case 0x4024:
				fds->write = v;
				fds->transfer = false;
				break;

			// FDS Control
			case 0x4025: {
				bool prev_motor = fds->motor;

				fds->motor = v & 0x01;
				fds->reset = v & 0x02;
				fds->mode = (v & 0x04) >> 2;
				fds->crc_ctrl = v & 0x10;
				fds->start = v & 0x40;
				fds->transfer_irq = v & 0x80;

				if (!prev_motor && fds->motor) {
					memset(&fds->file, 0, sizeof(struct fds_file));

					if (fds->delay < 50000)
						fds->delay = 50000;
				}

				cart_map_ciram(cart, (v & 0x08) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
				break;
			}

			// External connector
			case 0x4026:
				fds->ext = v;
				break;
		}

	// Audio - Wavetable
	} else if (addr >= 0x4040 && addr < 0x4080) {
		fds->audio.wtable[addr & 0x3F] = v;

	// Audio - Registers
	} else if (addr >= 0x4080 && addr <= 0x4097) {
		struct fds_audio *fdsa = &fds->audio;

		switch (addr) {
			case 0x4080:
			case 0x4084: {
				struct fds_env *env = addr == 0x4080 ? &fdsa->vol : &fdsa->mod;

				env->speed = v & 0x3F;
				env->increase = v & 0x40;
				env->enable = !(v & 0x80);

				fds_env_reset_timer(fdsa, env);

				if (!env->enable)
					env->gain = env->speed;
				break;
			}
			case 0x4082:
				fdsa->vol.freq = (fdsa->vol.freq & 0xF00) | v;
				break;
			case 0x4083:
				fdsa->vol.freq = (fdsa->vol.freq & 0x0FF) | ((uint16_t) (v & 0xF) << 8);
				fdsa->halt_env = v & 0x40;
				fdsa->halt_waveform = v & 0x80;

				if (fdsa->halt_env) {
					fds_env_reset_timer(fdsa, &fdsa->vol);
					fds_env_reset_timer(fdsa, &fdsa->mod);
				}
				break;
			case 0x4085:
				fds_update_counter(fdsa, v & 0x7F);
				break;
			case 0x4086:
				fdsa->mod.freq = (fdsa->mod.freq & 0xF00) | v;
				break;
			case 0x4087:
				fdsa->mod.freq = (fdsa->mod.freq & 0x0FF) | ((uint16_t) (v & 0xF) << 8);
				fdsa->mod_halt = v & 0x80;

				if (fdsa->mod_halt)
					fdsa->overflow = 0;
				break;
			case 0x4088:
				if (fdsa->mod_halt) {
					fdsa->mtable[fdsa->mpos & 0x3F] = v & 0x07;
					fdsa->mtable[(fdsa->mpos + 1) & 0x3F] = v & 0x07;
					fdsa->mpos = (fdsa->mpos + 2) & 0x3F;
				}
				break;
			case 0x4089:
				fdsa->mvol = v & 0x03;
				fdsa->wt_write = v & 0x80;
				break;
			case 0x408A:
				fdsa->mspeed = v;
				break;
		}

	// PRG RAM and BIOS
	} else if (addr < 0xE000) {
		cart_write(cart, PRG, addr, v);
	}
}

static uint8_t fds_prg_read(struct cart *cart, uint16_t addr, bool *mem_hit)
{
	struct fds *fds = cart_get_mapper(cart);

	*mem_hit = true;

	// Registers
	if (addr <= 0x4033) {
		switch (addr) {
			// Disk Status 0
			case 0x4030: {
				uint8_t v = 0;

				if (fds->irq.pending)
					v |= 0x01;

				if (fds->transfer)
					v |= 0x02;

				fds->irq.ack = true;

				fds->transfer = false;
				fds->ack_transfer = true;

				return v;
			}

			// Read data
			case 0x4031:
				fds->transfer = false;
				fds->ack_transfer = true;

				return fds->read;

			// Disk drive status
			case 0x4032: {
				uint8_t v = 0;

				bool rest = fds->file.eof || fds->file.pos == 0;

				if (!fds->disk_ready)
					v |= 0x05;

				if (!fds->disk_ready || rest)
					v |= 0x02;

				return v;
			}

			// External connector read
			case 0x4033:
				return fds->ext;

			// Open bus
			default:
				*mem_hit = false;
				break;
		}

	// Audio - Wavetable
	} else if (addr >= 0x4040 && addr < 0x4080) {
		return fds->audio.wtable[addr & 0x3F];

	// Audio - Registers
	} else if (addr >= 0x4080 && addr <= 0x4097) {
		struct fds_audio *fdsa = &fds->audio;

		switch (addr) {
			case 0x4090:
				return 0x80 | fdsa->vol.gain;
			case 0x4092:
				return 0x80 | fdsa->mod.gain;
			default:
				*mem_hit = false;
				break;
		}

		return 0;
	}

	return cart_read(cart, PRG, addr, mem_hit);
}


// Step

#define FDS_LEADING_GAP  (28300 / 8)
#define FDS_TRAILING_GAP (976 / 8)

static void fds_update_crc(uint16_t *crc, uint8_t v)
{
	for (uint16_t n = 0x01; n <= 0x80; n <<= 1) {
		uint8_t carry = *crc & 1;

		*crc >>= 1;

		if (carry)
			*crc ^= 0x8408;

		if (v & n)
			*crc ^= 0x8000;
	}
}

static uint8_t fds_file_read(const uint8_t *buf, size_t size, struct fds_file *f)
{
	// Set leading gap
	if (f->pos == 0)
		f->gap = FDS_LEADING_GAP;

	// In gap
	if (f->pos < f->gap) {
		f->pos++;
		return 0;
	}

	if (f->pos == f->gap) {
		f->pos++;
		return 0x80;
	}

	// EOF
	if (f->offset == size) {
		f->eof = true;
		return 0;
	}

	// Block data
	uint8_t v = buf[f->offset];

	if (f->pos < f->block) {
		f->pos++;
		f->offset++;

		fds_update_crc(&f->crc, v);

		return v;
	}

	// CRC at the end of each block
	if (f->pos == f->block) {
		f->pos++;

		fds_update_crc(&f->crc, 0);
		fds_update_crc(&f->crc, 0);

		return f->crc & 0xFF;
	}

	if (f->pos == f->block + 1) {
		f->gap = f->pos + FDS_TRAILING_GAP;
		f->pos++;

		// .qd format must skip past baked in CRC
		if (size == 0x10000)
			f->offset += 2;

		return (f->crc & 0xFF00) >> 8;
	}

	// Block start, reset CRC
	f->crc = 0x8000;
	f->zero = false;

	switch (v) {
		case 0:
			f->zero = true;
			break;
		case 1:
			f->block = f->pos + 56;
			break;
		case 2:
			f->block = f->pos + 2;
			break;
		case 3:
			f->block = f->pos + 16;
			f->data_len = buf[f->offset + 13] | ((uint16_t) buf[f->offset + 14] << 8);
			break;
		case 4:
			f->block = f->pos + f->data_len + 1;
			break;
		default:
			NES_Log("Invalid disk block %X: [%zu][%zu]", v, f->pos, f->offset);
			break;
	}

	f->pos++;
	f->offset++;

	fds_update_crc(&f->crc, v);

	return v;
}

static void fds_step_irqs(struct fds *fds, struct cpu *cpu)
{

	if (fds->irq.ack) {
		cpu_irq(cpu, IRQ_MAPPER, false);
		fds->irq.pending = false;
		fds->irq.ack = false;
	}

	if (fds->ack_transfer) {
		cpu_irq(cpu, IRQ_FDS, false);
		fds->ack_transfer = false;
	}

	if (fds->irq.enable) {
		if (fds->irq.counter == 0) {
			cpu_irq(cpu, IRQ_MAPPER, true);
			fds->irq.pending = true;
			fds->irq.counter = fds->irq.value;

			if (!fds->irq.reload)
				fds->irq.enable = false;

		} else {
			fds->irq.counter--;
		}
	}
}

static void fds_step(struct cart *cart, struct cpu *cpu, struct apu *apu)
{
	struct fds *fds = cart_get_mapper(cart);

	bool rest = fds->file.eof || fds->file.pos == 0;

	fds_step_irqs(fds, cpu);
	fds_step_audio(&fds->audio, apu);

	// Eject / insert disks
	if (fds->delay > 0) {
		fds->delay--;
		return;
	}

	if (!fds->disk_ready) {
		fds->disk = fds->inserted;
		fds->disk_ready = true;
		return;
	}

	if (rest && fds->disk != fds->inserted) {
		fds->disk_ready = false;
		fds->delay = 1000000;
		return;
	}

	// Do not scan if motor is shut off
	if (fds->disk == -1 || !fds->motor || (rest && fds->reset))
		return;

	// IO (scanning)
	bool gap = fds->file.pos <= fds->file.gap;
	bool gapr = fds->file.pos < fds->file.gap || fds->file.zero;
	bool gapw = fds->file.pos < fds->file.gap - 1;

	uint8_t *prg = cart_get_mem(cart, PRG_ROM);
	uint8_t *disk = prg + 0x2000 + fds->disk * fds->side_size;
	uint8_t v = fds_file_read(disk, fds->side_size, &fds->file);

	// The end
	if (fds->file.eof) {
		fds->motor = false;
		return;
	}

	// Read mode
	if (fds->mode == 1) {
		if (fds->start && !gapr) {
			fds->transfer = true;
			fds->read = v;

			if (!gap && fds->transfer_irq)
				cpu_irq(cpu, IRQ_FDS, true);
		}

	// Write mode
	} else {
		if (!fds->crc_ctrl && !gapw) {
			fds->transfer = true;

			if (!gap)
				disk[fds->file.offset - 1] = fds->start ? fds->write : 0;

			if (fds->transfer_irq)
				cpu_irq(cpu, IRQ_FDS, true);
		}
	}

	fds->delay = 160;
}
