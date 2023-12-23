#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if !defined(_MSC_VER)
	#include <strings.h>
#else
	#define strcasecmp _stricmp
#endif

#include "nes.h"

#include "deps/libretro.h"
#include "assets/db/nes20db.h"

#define RETRO_MAX_PATH 2048


// Globals

static NES *CTX;

static uint32_t CORE_SAMPLE_RATE;

static retro_environment_t RETRO_ENVIRONMENT;
static retro_video_refresh_t RETRO_VIDEO_REFRESH;
static retro_audio_sample_t RETRO_AUDIO_SAMPLE;
static retro_audio_sample_batch_t RETRO_AUDIO_SAMPLE_BATCH;
static retro_input_poll_t RETRO_INPUT_POLL;
static retro_input_state_t RETRO_INPUT_STATE;

static const struct retro_variable RETRO_VARIABLES[] = {
	{"merton-nes-nes_palette", "Palette; Kitrinx|Smooth|NES Classic|Composite Direct|PVM Style D93|PC-10|Sony CXA|Wavebeam"},
	{"merton-nes-nes_overclock", "Overclock; Off|On"},
	{"merton-nes-nes_sprite_limit", "Sprite Limit; On|Off"},
	{"merton-nes-nes_sample_rate", "Sample Rate; 48000|44100|22050|16000|11025|8000"},
	{"merton-nes-nes_high_pass", "High Pass Shift; 7|5|6|8|9"},
	{"merton-nes-nes_stereo", "Stereo; On|Off"},
	{NULL, NULL},
};


// Callbacks

static bool retro_cmd(unsigned cmd, void *data)
{
	return RETRO_ENVIRONMENT ? RETRO_ENVIRONMENT(cmd, data) : false;
}

static void retro_video(const uint32_t *frame, void *opaque)
{
	if (RETRO_VIDEO_REFRESH) {
		// Crop top + bottom overscan 8px
		RETRO_VIDEO_REFRESH(frame + (NES_FRAME_WIDTH * 8),
			NES_FRAME_WIDTH, NES_FRAME_HEIGHT - 16, NES_FRAME_WIDTH * 4);
	}
}

static void retro_audio(const int16_t *frames, uint32_t count, void *opaque)
{
	if (RETRO_AUDIO_SAMPLE_BATCH)
		RETRO_AUDIO_SAMPLE_BATCH(frames, count);
}

static void retro_log(const char *msg)
{
	struct retro_message data = {0};
	data.msg = msg;

	retro_cmd(RETRO_ENVIRONMENT_SET_MESSAGE, &data);
}


// Disks

static bool retro_set_eject_state(bool ejected)
{
	return false;
}

static bool retro_get_eject_state(void)
{
	return false;
}

static unsigned retro_get_image_index(void)
{
	if (!CTX)
		return 0;

	return NES_GetDisk(CTX);
}

static bool retro_set_image_index(unsigned index)
{
	if (!CTX)
		return false;

	return NES_SetDisk(CTX, (int8_t) index);
}

static unsigned retro_get_num_images(void)
{
	if (!CTX)
		return 0;

	return NES_GetNumDisks(CTX);
}

static bool retro_replace_image_index(unsigned index, const struct retro_game_info *info)
{
	return false;
}

static bool retro_add_image_index(void)
{
	return false;
}


// libretro

RETRO_API void retro_set_environment(retro_environment_t func)
{
	RETRO_ENVIRONMENT = func;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t func)
{
	RETRO_VIDEO_REFRESH = func;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t func)
{
	RETRO_AUDIO_SAMPLE = func;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t func)
{
	RETRO_AUDIO_SAMPLE_BATCH = func;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t func)
{
	RETRO_INPUT_POLL = func;
}

RETRO_API void retro_set_input_state(retro_input_state_t func)
{
	RETRO_INPUT_STATE = func;
}

static NES_Config retro_refresh_variables(void)
{
	NES_Config cfg = NES_CONFIG_DEFAULTS;

	for (size_t x = 0; x < SIZE_MAX; x++) {
		struct retro_variable var = {0};
		var.key = RETRO_VARIABLES[x].key;
		if (!var.key)
			break;

		if (retro_cmd(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
			if (!strcmp(var.key, "merton-nes-nes_palette")) {
				if (!strcmp(var.value, "Kitrinx")) {
					cfg.palette = NES_PALETTE_KITRINX;

				} else if (!strcmp(var.value, "Smooth")) {
					cfg.palette = NES_PALETTE_SMOOTH;

				} else if (!strcmp(var.value, "NES Classic")) {
					cfg.palette = NES_PALETTE_CLASSIC;

				} else if (!strcmp(var.value, "Composite Direct")) {
					cfg.palette = NES_PALETTE_COMPOSITE;

				} else if (!strcmp(var.value, "PVM Style D93")) {
					cfg.palette = NES_PALETTE_PVM_D93;

				} else if (!strcmp(var.value, "PC-10")) {
					cfg.palette = NES_PALETTE_PC10;

				} else if (!strcmp(var.value, "Sony CXA")) {
					cfg.palette = NES_PALETTE_SONY_CXA;

				} else if (!strcmp(var.value, "Wavebeam")) {
					cfg.palette = NES_PALETTE_WAVEBEAM;
				}

			} else if (!strcmp(var.key, "merton-nes-nes_overclock")) {
				if (!strcmp(var.value, "On")) {
					cfg.preNMI = cfg.postNMI = 131;

				} else {
					cfg.preNMI = cfg.postNMI = 0;
				}

			} else if (!strcmp(var.key, "merton-nes-nes_sprite_limit")) {
				cfg.maxSprites = !strcmp(var.value, "On") ? 8 : 64;

			} else if (!strcmp(var.key, "merton-nes-nes_sample_rate")) {
				cfg.sampleRate = atoi(var.value);

				if (cfg.sampleRate != CORE_SAMPLE_RATE) {
					CORE_SAMPLE_RATE = cfg.sampleRate;

					struct retro_system_av_info av_info = {0};
					retro_get_system_av_info(&av_info);
					retro_cmd(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
				}

			} else if (!strcmp(var.key, "merton-nes-nes_high_pass")) {
				cfg.highPass = (uint8_t) atoi(var.value);

			} else if (!strcmp(var.key, "merton-nes-nes_stereo")) {
				cfg.stereo = !strcmp(var.value, "On") ? true : false;
			}
		}
	}

	return cfg;
}

RETRO_API void retro_init(void)
{
	NES_Destroy(&CTX);
	NES_SetLogCallback(retro_log);

	enum retro_pixel_format data = RETRO_PIXEL_FORMAT_XRGB8888;
	retro_cmd(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &data);

	retro_cmd(RETRO_ENVIRONMENT_SET_VARIABLES, (void *) RETRO_VARIABLES);

	NES_Config cfg = retro_refresh_variables();
	CORE_SAMPLE_RATE = cfg.sampleRate;

	CTX = NES_Create(&cfg);
}

RETRO_API void retro_deinit(void)
{
	NES_Destroy(&CTX);
	NES_SetLogCallback(NULL);

	CORE_SAMPLE_RATE = 0;

	RETRO_ENVIRONMENT = NULL;
	RETRO_VIDEO_REFRESH = NULL;
	RETRO_AUDIO_SAMPLE = NULL;
	RETRO_AUDIO_SAMPLE_BATCH = NULL;
	RETRO_INPUT_POLL = NULL;
	RETRO_INPUT_STATE = NULL;
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	info->library_name = "NES";
	info->library_version = "1.0";
	info->valid_extensions = "nes|fds|qd";
	info->need_fullpath = false;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->geometry.base_width = NES_FRAME_WIDTH;
	info->geometry.base_height = NES_FRAME_HEIGHT;
	info->geometry.max_width = NES_FRAME_WIDTH;
	info->geometry.max_height = NES_FRAME_HEIGHT;
	info->geometry.aspect_ratio = 127.0f / 105.0f;

	info->timing.fps = 60.098812;
	info->timing.sample_rate = (double) CORE_SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

RETRO_API void retro_reset(void)
{
	if (!CTX)
		return;

	NES_Reset(CTX, false);
}

RETRO_API void retro_run(void)
{
	if (!CTX)
		return;

	bool update = false;
	if (retro_cmd(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &update) && update) {
		NES_Config cfg = retro_refresh_variables();
		NES_SetConfig(CTX, &cfg);
	}

	if (RETRO_INPUT_STATE) {
		if (RETRO_INPUT_POLL)
			RETRO_INPUT_POLL();

		#define GET_INPUT(player, button) \
			(RETRO_INPUT_STATE(player, RETRO_DEVICE_JOYPAD, 0, \
			RETRO_DEVICE_ID_JOYPAD_##button) ? NES_BUTTON_##button : 0)

		for (uint8_t x = 0; x < 2; x++) {
			uint8_t state = 0;
			state |= GET_INPUT(x, A);
			state |= GET_INPUT(x, B);
			state |= GET_INPUT(x, SELECT);
			state |= GET_INPUT(x, START);
			state |= GET_INPUT(x, UP);
			state |= GET_INPUT(x, DOWN);
			state |= GET_INPUT(x, LEFT);
			state |= GET_INPUT(x, RIGHT);

			NES_ControllerState(CTX, x, state);
		}
	}

	NES_NextFrame(CTX, retro_video, retro_audio, NULL);
}

RETRO_API size_t retro_serialize_size(void)
{
	if (!CTX)
		return 0;

	return NES_GetStateSize(CTX);
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	if (!CTX)
		return false;

	return NES_GetState(CTX, data, size);
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	if (!CTX)
		return false;

	return NES_SetState(CTX, data, size);
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

static bool retro_get_desc_from_db(size_t offset, uint32_t crc32, NES_CartDesc *desc)
{
	for (size_t x = 0; x < NES_DB_ROWS; x++) {
		const uint8_t *row = NES_DB + x * NES_DB_ROW_SIZE;

		if (crc32 == *((uint32_t *) row)) {
			NES_Log("0x%X found in DB", crc32);

			desc->offset = offset;
			desc->prgROMSize = row[4] * 0x4000;
			desc->chrROMSize = row[9] * 0x2000;
			desc->mapper = *((uint16_t *) (row + 14));
			desc->submapper = row[16] & 0xF;
			desc->mirror = (row[16] & 0x10) ? NES_MIRROR_VERTICAL :
				(row[16] & 0x20) ? NES_MIRROR_FOUR : NES_MIRROR_HORIZONTAL;
			desc->battery = row[16] & 0x80;
			desc->prgWRAMSize = *((uint16_t *) (row + 5)) * 8;
			desc->prgSRAMSize = *((uint16_t *) (row + 7)) * 8;
			desc->chrWRAMSize = *((uint16_t *) (row + 10)) * 8;
			desc->chrSRAMSize = *((uint16_t *) (row + 12)) * 8;

			return true;
		}
	}

	return false;
}

static uint32_t retro_crc32(uint32_t crc, const void *buf, size_t size)
{
	uint32_t table[0x100];

	for (uint32_t x = 0; x < 0x100; x++) {
		uint32_t r = x;

		for (uint8_t y = 0; y < 8; y++)
			r = (r & 1 ? 0 : 0xEDB88320) ^ r >> 1;

		table[x] = r ^ 0xFF000000;
	}

	for (size_t x = 0; x < size; x++)
		crc = table[(uint8_t) crc ^ ((const uint8_t *) buf)[x]] ^ crc >> 8;

	return crc;
}

static bool retro_is_fds(const char *path)
{
	if (!path)
		return false;

	const char *ext = strrchr(path, '.');
	if (!ext)
		return false;

	return !strcasecmp(ext, ".fds") || !strcasecmp(ext, ".qd");
}

static FILE *retro_fopen(const char *dir, const char *file)
{
	char full[RETRO_MAX_PATH];

	#if defined(_MSC_VER)
		snprintf(full, RETRO_MAX_PATH, "%s\\%s", dir, file);

		FILE *f = NULL;
		if (fopen_s(&f, full, "rb") == 0)
			return f;

		return NULL;

	#else
		snprintf(full, RETRO_MAX_PATH, "%s/%s", dir, file);

		return fopen(full, "rb");
	#endif
}

static bool retro_load_fds(const struct retro_game_info *game)
{
	const char *sys_dir = NULL;

	if (retro_cmd(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, (void *) &sys_dir) && sys_dir) {
		FILE *f = retro_fopen(sys_dir, "disksys.rom");

		if (f) {
			void *bios = calloc(0x2000, 1);
			size_t bsize = fread(bios, 1, 0x2000, f);

			bool r = NES_LoadDisks(CTX, bios, bsize, game->data, game->size);

			if (r) {
				struct retro_disk_control_callback cbs = {0};
				cbs.set_eject_state = retro_set_eject_state;
				cbs.get_eject_state = retro_get_eject_state;
				cbs.get_image_index = retro_get_image_index;
				cbs.set_image_index = retro_set_image_index;
				cbs.get_num_images = retro_get_num_images;
				cbs.replace_image_index = retro_replace_image_index;
				cbs.add_image_index = retro_add_image_index;

				retro_cmd(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &cbs);
			}

			free(bios);
			fclose(f);

			return r;
		}
	}

	NES_Log("Could not open FDS BIOS (disksys.rom)");

	return false;
}

static bool retro_load_rom(const struct retro_game_info *game)
{
	if (game->size <= 16)
		return false;

	const uint8_t *rom = game->data;
	size_t offset = 16 + ((rom[6] & 0x04) ? 512 : 0); // iNES and optional trainer

	if (game->size > offset) {
		uint32_t crc32 = retro_crc32(0, rom + offset, game->size - offset);

		NES_CartDesc desc = {0};
		bool found_in_db = retro_get_desc_from_db(offset, crc32, &desc);

		return NES_LoadCart(CTX, rom, game->size, found_in_db ? &desc : NULL);
	}

	return false;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
	// FDS
	if (retro_is_fds(game->path))
		return retro_load_fds(game);

	// ROM
	return retro_load_rom(game);
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game(void)
{
	if (!CTX)
		return;

	NES_LoadCart(CTX, NULL, 0, NULL);
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
	if (!CTX)
		return NULL;

	if (id != RETRO_MEMORY_SAVE_RAM)
		return NULL;

	return NES_GetSRAM(CTX);
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	if (!CTX)
		return 0;

	if (id != RETRO_MEMORY_SAVE_RAM)
		return 0;

	return NES_GetSRAMSize(CTX);
}
