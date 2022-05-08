import xml.etree.ElementTree
import struct

SUPPORTED = [1, 4, 206, 5, 9, 10, 18, 210, 19, 21, 22, 24, 26, 23, 25, 69, 85, 16,
	159, 0, 2, 3, 7, 11, 13, 30, 31, 34, 38, 66, 70, 71, 77, 78, 79, 87, 89, 93, 94,
	97, 101, 107, 111, 113, 140, 145, 146, 148, 149, 152, 180, 184, 185]

LICENSED = {}
ALL = {}

def xml_to_bin(name):
	data = []
	num_rows = 0

	for game in xml.etree.ElementTree.parse(name).getroot():
		name = game.get('name')

		rom = game.find('rom')
		prg = game.find('prgrom')
		prgram = game.find('prgram')
		prgnvram = game.find('prgnvram')
		chrrom = game.find('chrrom')
		chrram = game.find('chrram')
		chrnvram = game.find('chrnvram')
		pcb = game.find('pcb')

		prgrom_size = int(int(prg.get('size')) / 16384)
		chrrom_size = int(int(chrrom.get('size')) / 8192) if chrrom is not None else 0

		if prgrom_size < 256 and chrrom_size < 256:
			data += struct.pack('I', int(rom.get('crc32'), 16))
			data += struct.pack('B', prgrom_size)
			data += struct.pack('H', int(int(prgram.get('size')) / 8) if prgram is not None else 0)
			data += struct.pack('H', int(int(prgnvram.get('size')) / 8) if prgnvram is not None else 0)
			data += struct.pack('B', chrrom_size)
			data += struct.pack('H', int(int(chrram.get('size')) / 8) if chrram is not None else 0)
			data += struct.pack('H', int(int(chrnvram.get('size')) / 8) if chrnvram is not None else 0)

			# All mappers
			mapper = int(pcb.get('mapper'))
			if not ALL.get(mapper):
				ALL[mapper] = 0

			ALL[mapper] += 1

			# Licensed mappers
			if 'Licensed' in name:
				if not LICENSED.get(mapper):
					LICENSED[mapper] = 0

				LICENSED[mapper] += 1

			data += struct.pack('H', mapper)

			mirroring = pcb.get('mirroring')
			mirroring_bits = 0x10 if mirroring == 'V' else 0x20 if mirroring == '4' else 0x00
			data += struct.pack('B', int(pcb.get('submapper')) | mirroring_bits | (int(pcb.get('battery')) << 7))

			num_rows += 1

	return data, num_rows

if __name__ == '__main__':
	data, num_rows = xml_to_bin('nes20db.xml')
	row_len = int(len(data) / num_rows)

	f = open('nes20db.h', 'w')
	f.write('#pragma once\n\n')
	f.write('static const unsigned int NES_DB_ROWS = %u;\n' % num_rows)
	f.write('static const unsigned int NES_DB_ROW_SIZE = %u;\n' % row_len)
	f.write('static const unsigned char %s[] = {%s};\n' % ('NES_DB', ', '.join('{:d}'.format(x) for x in data)))

	n = 0
	n_support = 0
	for k, v in ALL.items():
		n += v
		if k in SUPPORTED:
			n_support += v

	nlicensed = 0
	nlicensed_support = 0
	for k, v in LICENSED.items():
		nlicensed += v
		if k in SUPPORTED:
			nlicensed_support += v

	print('Licensed:', '%d/%d' % (nlicensed_support, nlicensed), '(%.2f%%)' % (nlicensed_support / nlicensed * 100.0))
	print('Licensed + Unlicensed:', '%d/%d' % (n_support, n), '(%.2f%%)' % (n_support / n * 100.0))

	print('\nUnsupported Licensed:')
	for x in sorted(LICENSED.items(), key=lambda kv: kv[1], reverse=True):
		if x[0] not in SUPPORTED:
			print('Mapper:', x[0], '\tCount:', x[1])

	"""
	print('\nUnsupported:')
	for x in sorted(ALL.items(), key=lambda kv: kv[1], reverse=True):
		if x[0] not in SUPPORTED:
			print('Mapper:', x[0], '\tCount:', x[1])
	"""
