PALETTES = [
	['NES_PALETTE_KITRINX', 'Kitrinx34-Full'],
	['NES_PALETTE_SMOOTH', 'Smooth (FBX)'],
	['NES_PALETTE_CLASSIC', 'NES Classic (FBX)'],
	['NES_PALETTE_COMPOSITE', 'Composite Direct (FBX)'],
	['NES_PALETTE_PVM_D93', 'PVM Style D93 (FBX)'],
	['NES_PALETTE_PC10', 'PC-10'],
	['NES_PALETTE_SONY_CXA', 'Sony CXA'],
	['NES_PALETTE_WAVEBEAM', 'Wavebeam'],
]

if __name__ == '__main__':
	# Process palette files, create abgr hex color lists
	enums = []
	all_palettes = {}

	for const_name, file_name in PALETTES:
		data = open('%s.pal' % file_name, mode='rb').read()
		enums.append(const_name)

		x = 0
		colors = []
		while x < 64 * 3:
			a = 0xFF
			b = data[x + 2]
			g = data[x + 1]
			r = data[x + 0]
			colors.append('0x%02X%02X%02X%02X' % (a, r, g, b))
			x += 3

		all_palettes[const_name] = colors

	# Write a C header
	f = open('palette.h', 'w')

	# Enum
	f.write('typedef enum {\n')
	x = 0
	for e in enums:
		f.write('\t%s = %d,\n' % (e, x))
		x += 1
	f.write('} NES_Palette;\n\n')

	# Palette arrays
	num_palettes = len(enums)
	f.write('static const uint32_t PALETTES[%d][64] = {\n' % num_palettes)
	for e in enums:
		f.write('\t[%s] = {%s},\n' % (e, ', '.join(all_palettes[e])))
	f.write('};')
