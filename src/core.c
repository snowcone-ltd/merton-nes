#include "../../merton/src/core.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if !defined(_MSC_VER)
	#include <strings.h>
#else
	#define strcasecmp _stricmp
	#define strdup _strdup
#endif

#include "nes.h"
#include "settings.h"
#include "assets/db/nes20db.h"

struct Core {
	NES *nes;
	NES_Config cfg;
	NES_Button buttons;
	CoreVideoFunc video_func;
	CoreAudioFunc audio_func;
	void *video_opaque;
	void *audio_opaque;

	struct {
		char high_pass[16];
		char sample_rate[16];
	} settings;
};

static CoreLogFunc CORE_LOG_FUNC;
static void *CORE_LOG_OPAQUE;

void CoreUnloadGame(Core **core)
{
	if (!core || !*core)
		return;

	Core *ctx = *core;

	NES_Destroy(&ctx->nes);
	NES_SetLogCallback(NULL);

	free(ctx);
	*core = NULL;
}

static void core_log(const char *msg)
{
	if (CORE_LOG_FUNC)
		CORE_LOG_FUNC(msg, CORE_LOG_OPAQUE);
}

void CoreSetLogFunc(Core *ctx, CoreLogFunc func, void *opaque)
{
	CORE_LOG_FUNC = func;
	CORE_LOG_OPAQUE = opaque;

	NES_SetLogCallback(core_log);
}

void CoreSetAudioFunc(Core *ctx, CoreAudioFunc func, void *opaque)
{
	ctx->audio_func = func;
	ctx->audio_opaque = opaque;
}

void CoreSetVideoFunc(Core *ctx, CoreVideoFunc func, void *opaque)
{
	ctx->video_func = func;
	ctx->video_opaque = opaque;
}

static FILE *core_fopen(const char *path)
{
	#if defined(_MSC_VER)
		FILE *f = NULL;
		if (fopen_s(&f, path, "rb") == 0)
			return f;

		return NULL;

	#else
		return fopen(path, "rb");
	#endif
}

static void *core_read_file(const char *path, size_t max_size, size_t *size)
{
	FILE *f = core_fopen(path);
	if (!f)
		return NULL;

	void *data = calloc(max_size, 1);
	*size = fread(data, 1, max_size, f);

	fclose(f);

	return data;
}

static bool core_is_fds(const char *path)
{
	const char *ext = strrchr(path, '.');
	if (!ext)
		return false;

	return !strcasecmp(ext, ".fds") || !strcasecmp(ext, ".qd");
}

static uint32_t core_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
	crc = ~crc;

	for (size_t x = 0; x < len; x++) {
		crc = crc ^ buf[x];

		for (uint8_t y = 0; y < 8; y++)
			crc = (crc >> 1) ^ (0xEDB88320 & (0xFFFFFFFF ^ ((crc & 1) - 1)));
	}

	return ~crc;
}

static bool core_load_fds(Core *ctx, const char *system_dir, const uint8_t *rom, size_t size)
{
	char path[1024];
	snprintf(path, 1024, "%s\\%s", system_dir, "disksys.rom");

	size_t bsize = 0;
	void *bios = core_read_file(path, 0x2000, &bsize);

	if (!bios) {
		NES_Log("Could not open FDS BIOS (disksys.rom)");
		return false;
	}

	bool r = NES_LoadDisks(ctx->nes, bios, bsize, rom, size);

	free(bios);

	return r;
}

static bool core_get_desc_from_db(size_t offset, uint32_t crc32, NES_CartDesc *desc)
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

static bool core_load_rom(Core *ctx, const uint8_t *rom, size_t size)
{
	if (size <= 16)
		return false;

	size_t offset = 16 + ((rom[6] & 0x04) ? 512 : 0); // iNES and optional trainer

	if (size <= offset)
		return false;

	uint32_t crc32 = core_crc32(0, rom + offset, size - offset);

	NES_CartDesc desc = {0};
	bool found_in_db = core_get_desc_from_db(offset, crc32, &desc);

	return NES_LoadCart(ctx->nes, rom, size, found_in_db ? &desc : NULL);
}

static NES_Config core_load_settings(void)
{
	NES_Config cfg = (NES_Config) NES_CONFIG_DEFAULTS;

	cfg.palette =
		CMP_ENUM("palette", "Kitrinx") ? NES_PALETTE_KITRINX :
		CMP_ENUM("palette", "Smooth") ? NES_PALETTE_SMOOTH :
		CMP_ENUM("palette", "NES Classic") ? NES_PALETTE_CLASSIC :
		CMP_ENUM("palette", "Composite Direct") ? NES_PALETTE_COMPOSITE :
		CMP_ENUM("palette", "PVM Style D93") ? NES_PALETTE_PVM_D93 :
		CMP_ENUM("palette", "PC-10") ? NES_PALETTE_PC10 :
		CMP_ENUM("palette", "Sony CXA") ? NES_PALETTE_SONY_CXA :
		CMP_ENUM("palette", "Wavebeam") ? NES_PALETTE_WAVEBEAM :
		NES_PALETTE_KITRINX;

	cfg.preNMI = cfg.postNMI = CMP_BOOL("overclock") ? 131 : 0;
	cfg.maxSprites = CMP_BOOL("sprite-limit") ? 8 : 64;
	cfg.sampleRate = (uint32_t) atoi(GET_VAL("sample-rate"));
	cfg.highPass = (uint8_t) atoi(GET_VAL("high-pass"));
	cfg.stereo = CMP_BOOL("stereo");

	return cfg;
}

Core *CoreLoadGame(CoreSystem system, const char *systemDir, const char *path,
	const void *saveData, size_t saveDataSize)
{
	Core *ctx = calloc(1, sizeof(Core));
	ctx->cfg = core_load_settings();
	ctx->nes = NES_Create(&ctx->cfg);

	size_t size = 0;
	void *rom = core_read_file(path, 4 * 1024 * 1024, &size);

	bool loaded = core_is_fds(path) ? core_load_fds(ctx, systemDir, rom, size) :
		core_load_rom(ctx, rom, size);

	free(rom);

	if (loaded && saveData && saveDataSize <= NES_GetSRAMSize(ctx->nes))
		memcpy(NES_GetSRAM(ctx->nes), saveData, saveDataSize);

	if (!loaded)
		CoreUnloadGame(&ctx);

	return ctx;
}

double CoreGetFrameRate(Core *ctx)
{
	return 60.098812;
}

float CoreGetAspectRatio(Core *ctx)
{
	return 127.0f / 105.0f;
}

static void core_video(const uint32_t *frame, void *opaque)
{
	Core *ctx = opaque;

	if (ctx->video_func) {
		// Crop top + bottom overscan 8px
		ctx->video_func(frame + (NES_FRAME_WIDTH * 8), CORE_COLOR_FORMAT_BGRA,
			NES_FRAME_WIDTH, NES_FRAME_HEIGHT - 16, NES_FRAME_WIDTH * 4, ctx->video_opaque);
	}
}

static void core_audio(const int16_t *frames, uint32_t count, void *opaque)
{
	Core *ctx = opaque;

	if (ctx->audio_func)
		ctx->audio_func(frames, count, ctx->cfg.sampleRate, ctx->audio_opaque);
}

void CoreRun(Core *ctx)
{
	if (!ctx)
		return;

	NES_NextFrame(ctx->nes, core_video, core_audio, ctx);
}

void *CoreGetSaveData(Core *ctx, size_t *size)
{
	if (!ctx)
		return NULL;

	*size = NES_GetSRAMSize(ctx->nes);
	if (*size == 0)
		return NULL;

	void *sdata = malloc(*size);
	memcpy(sdata, NES_GetSRAM(ctx->nes), *size);

	return sdata;
}

void CoreReset(Core *ctx)
{
	if (!ctx)
		return;

	NES_Reset(ctx->nes, false);
}

void CoreSetButton(Core *ctx, uint8_t player, CoreButton button, bool pressed)
{
	if (!ctx)
		return;

	NES_Button nb = 0;

	switch (button) {
		case CORE_BUTTON_A:      nb = NES_BUTTON_B;      break;
		case CORE_BUTTON_B:      nb = NES_BUTTON_A;      break;
		case CORE_BUTTON_SELECT: nb = NES_BUTTON_SELECT; break;
		case CORE_BUTTON_START:  nb = NES_BUTTON_START;  break;
		case CORE_BUTTON_DPAD_U: nb = NES_BUTTON_UP;     break;
		case CORE_BUTTON_DPAD_D: nb = NES_BUTTON_DOWN;   break;
		case CORE_BUTTON_DPAD_L: nb = NES_BUTTON_LEFT;   break;
		case CORE_BUTTON_DPAD_R: nb = NES_BUTTON_RIGHT;  break;
		default:
			return;
	}

	if (pressed) {
		ctx->buttons |= nb;

	} else {
		ctx->buttons &= ~nb;
	}

	NES_ControllerState(ctx->nes, player, ctx->buttons);
}

void CoreSetAxis(Core *ctx, uint8_t player, CoreAxis axis, int16_t value)
{
}

void *CoreGetState(Core *ctx, size_t *size)
{
	if (!ctx)
		return NULL;

	*size = NES_GetStateSize(ctx->nes);
	if (*size == 0)
		return NULL;

	void *state = malloc(*size);
	NES_GetState(ctx->nes, state, *size);

	return state;
}

bool CoreSetState(Core *ctx, const void *state, size_t size)
{
	if (!ctx)
		return false;

	if (size != NES_GetStateSize(ctx->nes))
		return false;

	return NES_SetState(ctx->nes, state, size);
}

bool CoreInsertDisc(Core *ctx, const char *path)
{
	if (!ctx)
		return false;

	int8_t disk = NES_GetDisk(ctx->nes);
	if (disk < 0)
		return false;

	if (++disk == NES_GetNumDisks(ctx->nes))
		disk = 0;

	return NES_SetDisk(ctx->nes, disk);
}

CoreSetting *CoreGetSettings(uint32_t *len)
{
	*len = sizeof(CORE_SETTINGS) / sizeof(CoreSetting);

	return CORE_SETTINGS;
}

void CoreUpdateSettings(Core *ctx)
{
	if (!ctx)
		return;

	ctx->cfg = core_load_settings();
	NES_SetConfig(ctx->nes, &ctx->cfg);
}
