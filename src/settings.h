#define GET_VAL(name) \
	core_setting(name)->value

#define CMP_ENUM(name, val) \
	!strcmp(GET_VAL(name), val)

#define CMP_BOOL(name) \
	!strcmp(GET_VAL(name), "true")

static CoreSetting *core_setting(const char *name)
{
	uint32_t len = 0;
	CoreSetting *settings = CoreGetSettings(&len);

	for (uint32_t x = 0; x < len; x++) {
		CoreSetting *s = &settings[x];

		if (!strcmp(s->key, name))
			return s;
	}

	return NULL;
}

static CoreSetting CORE_SETTINGS[] = {{

	// General
	CORE_SYSTEM_NES,
	CORE_SETTING_BOOL,
	CORE_SETTING_GROUP_GENERAL,
	"Overclock",
	"overclock",
	{""},
	"false",
	0,
}, {
	CORE_SYSTEM_NES,
	CORE_SETTING_BOOL,
	CORE_SETTING_GROUP_GENERAL,
	"Sprite Limit",
	"sprite-limit",
	{""},
	"true",
	0,
}, {

	// Video
	CORE_SYSTEM_NES,
	CORE_SETTING_ENUM,
	CORE_SETTING_GROUP_VIDEO,
	"Palette",
	"palette",
	{"Kitrinx", "Smooth", "NES Classic", "Composite Direct",
		"PVM Style D93", "PC-10", "Sony CXA", "Wavebeam"},
	"Kitrinx",
	8,
}, {

	// Audio
	CORE_SYSTEM_NES,
	CORE_SETTING_ENUM,
	CORE_SETTING_GROUP_AUDIO,
	"Sample Rate",
	"sample-rate",
	{"48000", "44100", "22050", "16000", "11025", "8000"},
	"48000",
	6,
}, {
	CORE_SYSTEM_NES,
	CORE_SETTING_ENUM,
	CORE_SETTING_GROUP_AUDIO,
	"High Pass Shift",
	"high-pass",
	{"5", "6", "7", "8", "9"},
	"7",
	5,
}, {
	CORE_SYSTEM_NES,
	CORE_SETTING_BOOL,
	CORE_SETTING_GROUP_AUDIO,
	"Stereo",
	"stereo",
	{""},
	"true",
	0,
}};
