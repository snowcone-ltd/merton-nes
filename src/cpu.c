#include "cpu.h"

#include <stdlib.h>
#include <string.h>

enum cpu_flags {
	FLAG_C = 0x01, // Carry
	FLAG_Z = 0x02, // Zero
	FLAG_I = 0x04, // Interrupt disable
	FLAG_D = 0x08, // Decimal mode
	FLAG_B = 0x10, // Break command
	FLAG_U = 0x20, // Unused, set to 1 on startup
	FLAG_V = 0x40, // Overflow
	FLAG_N = 0x80, // Negative
};

enum irq_vector {
	NMI_VECTOR   = 0xFFFA,
	RESET_VECTOR = 0xFFFC,
	BRK_VECTOR   = 0xFFFE,
};

struct cpu {
	bool NMI;
	enum irq IRQ;
	bool irq_pending;
	bool halt;

	uint16_t PC; // Program counter
	uint8_t SP;  // Stack pointer
	uint8_t A;   // Accumulator
	uint8_t X;   // X index (general purpose)
	uint8_t Y;   // Y index (general purpose)
	uint8_t P;   // Status (flags)

	bool irq_p2;
	bool nmi_p2;
	bool nmi_signal;
};


// Addressing

#define PAGEX(a, b) \
	(((a) & 0xFF00) != ((b) & 0xFF00))

enum address_mode {
	MODE_IMPLIED     = 0,
	MODE_ACCUMULATOR = 1,
	MODE_IMMEDIATE   = 2,
	MODE_RELATIVE    = 3,
	MODE_ZERO_PAGE   = 4,
	MODE_ZERO_PAGE_X = 5,
	MODE_ZERO_PAGE_Y = 6,
	MODE_ABSOLUTE    = 7,
	MODE_ABSOLUTE_X  = 8,
	MODE_ABSOLUTE_Y  = 9,
	MODE_INDIRECT    = 10,
	MODE_INDIRECT_X  = 11, // Indexed Indirect
	MODE_INDIRECT_Y  = 12, // Indirect Indexed
};

enum io_mode {
	IO_NONE = 0,
	IO_R,     // Read
	IO_W,     // Write
	IO_RMW,   // Read-modify-write
	IO_STACK, // Pushes/pulls from the stack
};

static uint16_t cpu_read16(NES *nes, uint16_t addr)
{
	uint16_t h = (uint16_t) sys_read_cycle(nes, addr + 1) << 8;
	uint16_t l = (uint16_t) sys_read_cycle(nes, addr);

	return h | l;
}

static void cpu_indexed_dummy_read(NES *nes, enum io_mode io_mode, bool pagex, uint16_t addr)
{
	if (io_mode == IO_RMW || io_mode == IO_W) {
		sys_read_cycle(nes, pagex ? addr - 0x0100 : addr);

	} else if (io_mode == IO_R && pagex) {
		sys_read_cycle(nes, addr - 0x0100);
	}
}

static uint16_t cpu_opcode_address(struct cpu *cpu, NES *nes,
	enum address_mode mode, enum io_mode io_mode, bool *pagex)
{
	uint16_t addr = 0;

	switch (mode) {
		case MODE_IMPLIED:
		case MODE_ACCUMULATOR:
			sys_read_cycle(nes, cpu->PC); // Dummy read
			break;

		case MODE_IMMEDIATE:
			addr = cpu->PC++;
			break;

		case MODE_RELATIVE: // Offset between -128 and +127
		case MODE_ZERO_PAGE:
			addr = sys_read_cycle(nes, cpu->PC++);
			break;

		case MODE_ZERO_PAGE_X: {
			uint8_t iaddr = sys_read_cycle(nes, cpu->PC++);
			sys_read_cycle(nes, iaddr); // Dummy read
			addr = ((uint16_t) iaddr + cpu->X) & 0x00FF;
			break;

		} case MODE_ZERO_PAGE_Y: {
			uint8_t iaddr = sys_read_cycle(nes, cpu->PC++);
			sys_read_cycle(nes, iaddr); // Dummy read
			addr = ((uint16_t) iaddr + cpu->Y) & 0x00FF;
			break;

		} case MODE_ABSOLUTE:
			addr = cpu_read16(nes, cpu->PC);
			cpu->PC += 2;
			break;

		case MODE_ABSOLUTE_X:
			addr = cpu_read16(nes, cpu->PC) + cpu->X;
			cpu->PC += 2;

			*pagex = PAGEX(addr - cpu->X, addr);
			cpu_indexed_dummy_read(nes, io_mode, *pagex, addr);
			break;

		case MODE_ABSOLUTE_Y:
			addr = cpu_read16(nes, cpu->PC) + cpu->Y;
			cpu->PC += 2;

			*pagex = PAGEX(addr - cpu->Y, addr);
			cpu_indexed_dummy_read(nes, io_mode, *pagex, addr);
			break;

		case MODE_INDIRECT: {
			uint16_t iaddr = cpu_read16(nes, cpu->PC);
			cpu->PC += 2;

			uint8_t addrl = sys_read_cycle(nes, iaddr);
			uint8_t addrh = sys_read_cycle(nes, (iaddr & 0xFF00) | ((iaddr + 1) % 0x0100));
			addr = (uint16_t) addrl | ((uint16_t) addrh << 8);
			break;

		} case MODE_INDIRECT_X: {
			uint8_t pointer = sys_read_cycle(nes, cpu->PC++);
			sys_read_cycle(nes, pointer); //  Dummy read
			uint8_t pointerx = (pointer + cpu->X) & 0x00FF;
			uint8_t addrl = sys_read_cycle(nes, pointerx);
			uint8_t addrh = sys_read_cycle(nes, (pointerx + 1) & 0x00FF);
			addr = (uint16_t) addrl | ((uint16_t) addrh << 8);
			break;

		} case MODE_INDIRECT_Y: {
			uint8_t pointer = sys_read_cycle(nes, cpu->PC++);
			uint8_t addrl = sys_read_cycle(nes, pointer);
			uint8_t addrh = sys_read_cycle(nes, (pointer + 1) & 0x00FF);
			addr = ((uint16_t) addrl | ((uint16_t) addrh << 8)) + cpu->Y;

			*pagex = PAGEX(addr - cpu->Y, addr);
			cpu_indexed_dummy_read(nes, io_mode, *pagex, addr);
			break;
		}
	}

	return addr;
}


// Opcodes

enum opcode_name {
	SEI = 1, CLD, LDA, STA, LDX, TXS, AND, BEQ, LDY, STY, DEY,
	BNE, DEC, BPL, JSR, JMP, PHA, TXA, TYA, CMP, LSR, TAX, CLC,
	ADC, DEX, INY, RTS, PLA, TAY, EOR, ROR, ORA, STX, ASL, INX,
	BCS, BMI, BCC, SEC, RTI, INC, SBC, CPY, CPX, PHP, PLP, ROL,
	BRK, TSX, BIT, CLI, NOP, SED, CLV, BVC, BVS,

	// UNOFFICIAL
	DOP, AAC, ASR, ARR, ATX, AXS, SLO, RLA, SRE, RRA, AAX, LAX,
	DCP, ISC, TOP, SYA, SXA, XAA, AXA, LAR, XAS,
};

struct opcode {
	const char *name;
	enum address_mode mode;
	enum opcode_name lookup;
	enum io_mode io_mode;
};

#define SET_OP(_code, _name, _mode, _io) \
	[_code] = {#_name, _mode, _name, _io},

static const struct opcode OP[0x100] = {
	// http://nesdev.com/6502_cpu.txt -- The best reference
	// http://www.obelisk.me.uk/6502/reference.html

	SET_OP(0xA9, LDA, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xA5, LDA, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xB5, LDA, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xAD, LDA, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xBD, LDA, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0xB9, LDA, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0xA1, LDA, MODE_INDIRECT_X,  IO_R)
	SET_OP(0xB1, LDA, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0xA2, LDX, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xA6, LDX, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xB6, LDX, MODE_ZERO_PAGE_Y, IO_R)
	SET_OP(0xAE, LDX, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xBE, LDX, MODE_ABSOLUTE_Y,  IO_R)

	SET_OP(0xA0, LDY, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xA4, LDY, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xB4, LDY, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xAC, LDY, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xBC, LDY, MODE_ABSOLUTE_X,  IO_R)

	SET_OP(0x29, AND, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x25, AND, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x35, AND, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x2D, AND, MODE_ABSOLUTE,    IO_R)
	SET_OP(0x3D, AND, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x39, AND, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0x21, AND, MODE_INDIRECT_X,  IO_R)
	SET_OP(0x31, AND, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0x49, EOR, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x45, EOR, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x55, EOR, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x4D, EOR, MODE_ABSOLUTE,    IO_R)
	SET_OP(0x5D, EOR, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x59, EOR, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0x41, EOR, MODE_INDIRECT_X,  IO_R)
	SET_OP(0x51, EOR, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0xC9, CMP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xC5, CMP, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xD5, CMP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xCD, CMP, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xDD, CMP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0xD9, CMP, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0xC1, CMP, MODE_INDIRECT_X,  IO_R)
	SET_OP(0xD1, CMP, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0xC0, CPY, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xC4, CPY, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xCC, CPY, MODE_ABSOLUTE,    IO_R)

	SET_OP(0xE0, CPX, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xE4, CPX, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xEC, CPX, MODE_ABSOLUTE,    IO_R)

	SET_OP(0x69, ADC, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x65, ADC, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x75, ADC, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x6D, ADC, MODE_ABSOLUTE,    IO_R)
	SET_OP(0x7D, ADC, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x79, ADC, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0x61, ADC, MODE_INDIRECT_X,  IO_R)
	SET_OP(0x71, ADC, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0xE9, SBC, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xE5, SBC, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xF5, SBC, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xED, SBC, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xFD, SBC, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0xF9, SBC, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0xE1, SBC, MODE_INDIRECT_X,  IO_R)
	SET_OP(0xF1, SBC, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0x09, ORA, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x05, ORA, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x15, ORA, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x0D, ORA, MODE_ABSOLUTE,    IO_R)
	SET_OP(0x1D, ORA, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x19, ORA, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0x01, ORA, MODE_INDIRECT_X,  IO_R)
	SET_OP(0x11, ORA, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0x24, BIT, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x2C, BIT, MODE_ABSOLUTE,    IO_R)

	SET_OP(0x85, STA, MODE_ZERO_PAGE,   IO_W)
	SET_OP(0x95, STA, MODE_ZERO_PAGE_X, IO_W)
	SET_OP(0x8D, STA, MODE_ABSOLUTE,    IO_W)
	SET_OP(0x9D, STA, MODE_ABSOLUTE_X,  IO_W)
	SET_OP(0x99, STA, MODE_ABSOLUTE_Y,  IO_W)
	SET_OP(0x81, STA, MODE_INDIRECT_X,  IO_W)
	SET_OP(0x91, STA, MODE_INDIRECT_Y,  IO_W)

	SET_OP(0x86, STX, MODE_ZERO_PAGE,   IO_W)
	SET_OP(0x96, STX, MODE_ZERO_PAGE_Y, IO_W)
	SET_OP(0x8E, STX, MODE_ABSOLUTE,    IO_W)

	SET_OP(0x84, STY, MODE_ZERO_PAGE,   IO_W)
	SET_OP(0x94, STY, MODE_ZERO_PAGE_X, IO_W)
	SET_OP(0x8C, STY, MODE_ABSOLUTE,    IO_W)

	SET_OP(0xC6, DEC, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0xD6, DEC, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0xCE, DEC, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0xDE, DEC, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0xEE, INC, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0xE6, INC, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0xF6, INC, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0xFE, INC, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0x4A, LSR, MODE_ACCUMULATOR, IO_NONE)
	SET_OP(0x46, LSR, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x56, LSR, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x4E, LSR, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x5E, LSR, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0x0A, ASL, MODE_ACCUMULATOR, IO_NONE)
	SET_OP(0x06, ASL, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x16, ASL, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x0E, ASL, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x1E, ASL, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0x6A, ROR, MODE_ACCUMULATOR, IO_NONE)
	SET_OP(0x66, ROR, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x76, ROR, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x6E, ROR, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x7E, ROR, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0x2A, ROL, MODE_ACCUMULATOR, IO_NONE)
	SET_OP(0x26, ROL, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x36, ROL, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x2E, ROL, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x3E, ROL, MODE_ABSOLUTE_X,  IO_RMW)

	SET_OP(0xF0, BEQ, MODE_RELATIVE,    IO_NONE)
	SET_OP(0xD0, BNE, MODE_RELATIVE,    IO_NONE)
	SET_OP(0x10, BPL, MODE_RELATIVE,    IO_NONE)
	SET_OP(0x30, BMI, MODE_RELATIVE,    IO_NONE)
	SET_OP(0xB0, BCS, MODE_RELATIVE,    IO_NONE)
	SET_OP(0x90, BCC, MODE_RELATIVE,    IO_NONE)
	SET_OP(0x50, BVC, MODE_RELATIVE,    IO_NONE)
	SET_OP(0x70, BVS, MODE_RELATIVE,    IO_NONE)

	SET_OP(0x00, BRK, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x40, RTI, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x48, PHA, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x08, PHP, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x68, PLA, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x28, PLP, MODE_IMPLIED,     IO_STACK)
	SET_OP(0x78, SEI, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xF8, SED, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xD8, CLD, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x58, CLI, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x9A, TXS, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x88, DEY, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xAA, TAX, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xA8, TAY, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x8A, TXA, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x98, TYA, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xBA, TSX, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x60, RTS, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x18, CLC, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xB8, CLV, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xCA, DEX, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x38, SEC, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xE8, INX, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xC8, INY, MODE_IMPLIED,     IO_NONE)

	SET_OP(0x20, JSR, MODE_ABSOLUTE,    IO_STACK)

	SET_OP(0x4C, JMP, MODE_ABSOLUTE,    IO_NONE)
	SET_OP(0x6C, JMP, MODE_INDIRECT,    IO_NONE)

	SET_OP(0xEA, NOP, MODE_IMPLIED,     IO_NONE)


	// UNOFFICIAL -- Not used by nearly any games, but good for testing
	// http://nesdev.com/undocumented_opcodes.txt

	SET_OP(0xEB, SBC, MODE_IMMEDIATE,   IO_R)

	SET_OP(0x80, DOP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x82, DOP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x89, DOP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xC2, DOP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0xE2, DOP, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x04, DOP, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x44, DOP, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x64, DOP, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0x14, DOP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x34, DOP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x54, DOP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0x74, DOP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xD4, DOP, MODE_ZERO_PAGE_X, IO_R)
	SET_OP(0xF4, DOP, MODE_ZERO_PAGE_X, IO_R)

	SET_OP(0x0C, TOP, MODE_ABSOLUTE,    IO_R)
	SET_OP(0x1C, TOP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x3C, TOP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x5C, TOP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0x7C, TOP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0xDC, TOP, MODE_ABSOLUTE_X,  IO_R)
	SET_OP(0xFC, TOP, MODE_ABSOLUTE_X,  IO_R)

	SET_OP(0xA7, LAX, MODE_ZERO_PAGE,   IO_R)
	SET_OP(0xB7, LAX, MODE_ZERO_PAGE_Y, IO_R)
	SET_OP(0xAF, LAX, MODE_ABSOLUTE,    IO_R)
	SET_OP(0xBF, LAX, MODE_ABSOLUTE_Y,  IO_R)
	SET_OP(0xA3, LAX, MODE_INDIRECT_X,  IO_R)
	SET_OP(0xB3, LAX, MODE_INDIRECT_Y,  IO_R)

	SET_OP(0x0B, AAC, MODE_IMMEDIATE,   IO_R)
	SET_OP(0x2B, AAC, MODE_IMMEDIATE,   IO_R)

	SET_OP(0x4B, ASR, MODE_IMMEDIATE,   IO_R)

	SET_OP(0x6B, ARR, MODE_IMMEDIATE,   IO_R)

	SET_OP(0xAB, ATX, MODE_IMMEDIATE,   IO_R)

	SET_OP(0xCB, AXS, MODE_IMMEDIATE,   IO_R)

	SET_OP(0x8B, XAA, MODE_IMMEDIATE,   IO_R)

	SET_OP(0xBB, LAR, MODE_ABSOLUTE_Y,  IO_R)

	SET_OP(0x87, AAX, MODE_ZERO_PAGE,   IO_W)
	SET_OP(0x97, AAX, MODE_ZERO_PAGE_Y, IO_W)
	SET_OP(0x8F, AAX, MODE_ABSOLUTE,    IO_W)
	SET_OP(0x83, AAX, MODE_INDIRECT_X,  IO_W)

	SET_OP(0x9F, AXA, MODE_ABSOLUTE_Y,  IO_W)
	SET_OP(0x93, AXA, MODE_INDIRECT_Y,  IO_W)

	SET_OP(0x9C, SYA, MODE_ABSOLUTE_X,  IO_W)

	SET_OP(0x9E, SXA, MODE_ABSOLUTE_Y,  IO_W)

	SET_OP(0x9B, XAS, MODE_ABSOLUTE_Y,  IO_W)

	SET_OP(0x07, SLO, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x17, SLO, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x0F, SLO, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x1F, SLO, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0x1B, SLO, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0x03, SLO, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0x13, SLO, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0x27, RLA, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x37, RLA, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x2F, RLA, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x3F, RLA, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0x3B, RLA, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0x23, RLA, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0x33, RLA, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0x47, SRE, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x57, SRE, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x4F, SRE, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x5F, SRE, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0x5B, SRE, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0x43, SRE, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0x53, SRE, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0x67, RRA, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0x77, RRA, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0x6F, RRA, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0x7F, RRA, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0x7B, RRA, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0x63, RRA, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0x73, RRA, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0xC7, DCP, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0xD7, DCP, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0xCF, DCP, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0xDF, DCP, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0xDB, DCP, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0xC3, DCP, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0xD3, DCP, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0xE7, ISC, MODE_ZERO_PAGE,   IO_RMW)
	SET_OP(0xF7, ISC, MODE_ZERO_PAGE_X, IO_RMW)
	SET_OP(0xEF, ISC, MODE_ABSOLUTE,    IO_RMW)
	SET_OP(0xFF, ISC, MODE_ABSOLUTE_X,  IO_RMW)
	SET_OP(0xFB, ISC, MODE_ABSOLUTE_Y,  IO_RMW)
	SET_OP(0xE3, ISC, MODE_INDIRECT_X,  IO_RMW)
	SET_OP(0xF3, ISC, MODE_INDIRECT_Y,  IO_RMW)

	SET_OP(0x1A, NOP, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x3A, NOP, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x5A, NOP, MODE_IMPLIED,     IO_NONE)
	SET_OP(0x7A, NOP, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xDA, NOP, MODE_IMPLIED,     IO_NONE)
	SET_OP(0xFA, NOP, MODE_IMPLIED,     IO_NONE)
};


// Stack Helpers

static uint8_t cpu_read_sp(struct cpu *cpu, NES *nes)
{
	return sys_read_cycle(nes, 0x0100 | (uint16_t) cpu->SP);
}

static uint8_t cpu_pull(struct cpu *cpu, NES *nes)
{
	cpu->SP++;

	return cpu_read_sp(cpu, nes);
}

static void cpu_push(struct cpu *cpu, NES *nes, uint8_t val)
{
	sys_write_cycle(nes, 0x0100 | (uint16_t) cpu->SP--, val);
}

static uint16_t cpu_pull16(struct cpu *cpu, NES *nes)
{
	uint16_t lo = (uint16_t) cpu_pull(cpu, nes);
	uint16_t hi = (uint16_t) cpu_pull(cpu, nes);

	return (hi << 8) | lo;
}

static void cpu_push16(struct cpu *cpu, NES *nes, uint16_t val)
{
	cpu_push(cpu, nes, (uint8_t) (val >> 8) & 0x00ff);
	cpu_push(cpu, nes, (uint8_t) (val & 0x00ff));
}


// Flag Helpers

static void cpu_test_flag(struct cpu *cpu, uint32_t flag, bool test)
{
	if (test) {
		SET_FLAG(cpu->P, flag);
	} else {
		UNSET_FLAG(cpu->P, flag);
	}
}

static void cpu_eval_Z(struct cpu *cpu, uint8_t val)
{
	cpu_test_flag(cpu, FLAG_Z, val == 0);
}

static void cpu_eval_N(struct cpu *cpu, uint8_t val)
{
	// Test high bit for negative value
	cpu_test_flag(cpu, FLAG_N, val & 0x80);
}

static void cpu_eval_ZN(struct cpu *cpu, uint8_t val)
{
	cpu_eval_Z(cpu, val);
	cpu_eval_N(cpu, val);
}


// Instructions

static uint8_t cpu_lsr(struct cpu *cpu, NES *nes, enum address_mode mode, uint16_t addr)
{
	if (mode == MODE_ACCUMULATOR) {
		cpu_test_flag(cpu, FLAG_C, cpu->A & 0x01);
		cpu->A >>= 1;
		cpu_eval_ZN(cpu, cpu->A);

	} else {
		uint8_t val = sys_read_cycle(nes, addr);
		sys_write_cycle(nes, addr, val); // Dummy write
		cpu_test_flag(cpu, FLAG_C, val & 0x01);
		val >>= 1;
		sys_write_cycle(nes, addr, val);
		cpu_eval_ZN(cpu, val);

		return val;
	}

	return 0;
}

static uint8_t cpu_asl(struct cpu *cpu, NES *nes, enum address_mode mode, uint16_t addr)
{
	if (mode == MODE_ACCUMULATOR) {
		cpu_test_flag(cpu, FLAG_C, (cpu->A >> 7) & 0x01);
		cpu->A <<= 1;
		cpu_eval_ZN(cpu, cpu->A);

	} else {
		uint8_t val = sys_read_cycle(nes, addr);
		sys_write_cycle(nes, addr, val); // Dummy write
		cpu_test_flag(cpu, FLAG_C, (val >> 7) & 0x01);
		val <<= 1;
		sys_write_cycle(nes, addr, val);
		cpu_eval_ZN(cpu, val);

		return val;
	}

	return 0;
}

static uint8_t cpu_rol(struct cpu *cpu, NES *nes, enum address_mode mode, uint16_t addr)
{
	uint8_t c = GET_FLAG(cpu->P, FLAG_C) ? 0x01 : 0x00;

	if (mode == MODE_ACCUMULATOR) {
		cpu_test_flag(cpu, FLAG_C, (cpu->A >> 7) & 0x01);
		cpu->A = (cpu->A << 1) | c;
		cpu_eval_ZN(cpu, cpu->A);

	} else {
		uint8_t val = sys_read_cycle(nes, addr);
		sys_write_cycle(nes, addr, val); // Dummy write
		cpu_test_flag(cpu, FLAG_C, (val >> 7) & 0x01);
		val = (val << 1) | c;
		sys_write_cycle(nes, addr, val);
		cpu_eval_ZN(cpu, val);

		return val;
	}

	return 0;
}

static uint8_t cpu_ror(struct cpu *cpu, NES *nes, enum address_mode mode, uint16_t addr)
{
	uint8_t c = GET_FLAG(cpu->P, FLAG_C) ? 0x01 : 0x00;

	if (mode == MODE_ACCUMULATOR) {
		cpu_test_flag(cpu, FLAG_C, cpu->A & 0x01);
		cpu->A = (cpu->A >> 1) | (c << 7);
		cpu_eval_ZN(cpu, cpu->A);

	} else {
		uint8_t val = sys_read_cycle(nes, addr);
		sys_write_cycle(nes, addr, val); // Dummy write
		cpu_test_flag(cpu, FLAG_C, val & 0x01);
		val = (val >> 1) | (c << 7);
		sys_write_cycle(nes, addr, val);
		cpu_eval_ZN(cpu, val);

		return val;
	}

	return 0;
}

static uint8_t cpu_inc(struct cpu *cpu, NES *nes, uint16_t addr)
{
	uint8_t val = sys_read_cycle(nes, addr);
	sys_write_cycle(nes, addr, val); // Dummy write

	val += 1;
	sys_write_cycle(nes, addr, val);
	cpu_eval_ZN(cpu, val);

	return val;
}

static uint8_t cpu_dec(struct cpu *cpu, NES *nes, uint16_t addr)
{
	uint8_t val = sys_read_cycle(nes, addr) ;
	sys_write_cycle(nes, addr, val); // Dummy write

	val -= 1;
	sys_write_cycle(nes, addr, val);
	cpu_eval_ZN(cpu, val);

	return val;
}

static void cpu_sxa_sya(NES *nes, uint16_t addr, uint8_t r)
{
	uint8_t addr_high = addr >> 8;

	sys_write_cycle(nes, ((r & (addr_high + 1)) << 8) | (addr & 0xFF), r & (addr_high + 1));
}

static void cpu_and(struct cpu *cpu, uint8_t v)
{
	cpu->A &= v;
	cpu_eval_ZN(cpu, cpu->A);
}

static void cpu_ora(struct cpu *cpu, uint8_t v)
{
	cpu->A |= v;
	cpu_eval_ZN(cpu, cpu->A);
}

static void cpu_eor(struct cpu *cpu, uint8_t v)
{
	cpu->A ^= v;
	cpu_eval_ZN(cpu, cpu->A);
}

static void cpu_adc(struct cpu *cpu, uint8_t v)
{
	uint8_t a = cpu->A;
	uint8_t c = GET_FLAG(cpu->P, FLAG_C) ? 1 : 0;

	cpu->A = a + v + c;
	cpu_eval_ZN(cpu, cpu->A);

	cpu_test_flag(cpu, FLAG_C, a + v + c > 0xff);
	cpu_test_flag(cpu, FLAG_V, !((a ^ v) & 0x80) && ((a ^ cpu->A) & 0x80));
}

static void cpu_sbc(struct cpu *cpu, uint8_t v)
{
	uint8_t a = cpu->A;
	uint8_t c = GET_FLAG(cpu->P, FLAG_C) ? 1 : 0;

	cpu->A = a - v - (1 - c);
	cpu_eval_ZN(cpu, cpu->A);

	cpu_test_flag(cpu, FLAG_C, a - v - (1 - c) >= 0);
	cpu_test_flag(cpu, FLAG_V, ((a ^ v) & 0x80) && ((a ^ cpu->A) & 0x80));
}

static void cpu_branch(struct cpu *cpu, NES *nes, uint16_t addr)
{
	bool irq_was_pending = cpu->irq_pending;
	sys_read_cycle(nes, cpu->PC);

	// First try the un-pagecrossed version of the address
	uint16_t target_pc = cpu->PC + (int8_t) addr;
	cpu->PC = (cpu->PC & 0xFF00) | (target_pc & 0x00FF);

	// Branching to a new page always requires another read
	if (target_pc != cpu->PC) {
		sys_read_cycle(nes, cpu->PC);
		cpu->PC = target_pc;

	// On a taken non-page crossing branch, the tick above does NOT poll for IRQ
	// https://wiki.nesdev.com/w/index.php/CPU_interrupts
	} else {
		cpu->irq_pending = irq_was_pending;
	}
}

static bool cpu_exec(struct cpu *cpu, NES *nes)
{
	uint8_t code = sys_read_cycle(nes, cpu->PC++);
	const struct opcode *op = &OP[code];

	bool pagex = false;
	uint16_t addr = cpu_opcode_address(cpu, nes, op->mode, op->io_mode, &pagex);

	switch (op->lookup) {
		case SEI:
			SET_FLAG(cpu->P, FLAG_I);
			break;

		case CLI:
			UNSET_FLAG(cpu->P, FLAG_I);
			break;

		case SED:
			SET_FLAG(cpu->P, FLAG_D);
			break;

		case CLD:
			UNSET_FLAG(cpu->P, FLAG_D);
			break;

		case SEC:
			SET_FLAG(cpu->P, FLAG_C);
			break;

		case CLC:
			UNSET_FLAG(cpu->P, FLAG_C);
			break;

		case CLV:
			UNSET_FLAG(cpu->P, FLAG_V);
			break;

		case LDA:
			cpu->A = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case STA:
			sys_write_cycle(nes, addr, cpu->A);
			break;

		case LDX:
			cpu->X = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->X);
			break;

		case TXS:
			cpu->SP = cpu->X;
			break;

		case AND:
			cpu_and(cpu, sys_read_cycle(nes, addr));
			break;

		case BEQ:
			if (GET_FLAG(cpu->P, FLAG_Z))
				cpu_branch(cpu, nes, addr);
			break;

		case BVC:
			if (!GET_FLAG(cpu->P, FLAG_V))
				cpu_branch(cpu, nes, addr);
			break;

		case BVS:
			if (GET_FLAG(cpu->P, FLAG_V))
				cpu_branch(cpu, nes, addr);
			break;

		case BNE:
			if (!GET_FLAG(cpu->P, FLAG_Z))
				cpu_branch(cpu, nes, addr);
			break;

		case BMI:
			if (GET_FLAG(cpu->P, FLAG_N))
				cpu_branch(cpu, nes, addr);
			break;

		case BPL:
			if (!GET_FLAG(cpu->P, FLAG_N))
				cpu_branch(cpu, nes, addr);
			break;

		case BCS:
			if (GET_FLAG(cpu->P, FLAG_C))
				cpu_branch(cpu, nes, addr);
			break;

		case BCC:
			if (!GET_FLAG(cpu->P, FLAG_C))
				cpu_branch(cpu, nes, addr);
			break;

		case LDY:
			cpu->Y = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->Y);
			break;

		case STY:
			sys_write_cycle(nes, addr, cpu->Y);
			break;

		case DEY:
			cpu->Y -= 1;
			cpu_eval_ZN(cpu, cpu->Y);
			break;

		case DEC:
			cpu_dec(cpu, nes, addr);
			break;

		case JSR:
			cpu_read_sp(cpu, nes); // Internal operation (predecrement S?)
			cpu_push16(cpu, nes, cpu->PC - 1);
			cpu->PC = addr;
			break;

		case JMP:
			cpu->PC = addr;
			break;

		case PHA:
			cpu_push(cpu, nes, cpu->A);
			break;

		case TXA:
			cpu->A = cpu->X;
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case TYA:
			cpu->A = cpu->Y;
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case CMP: {
			uint8_t val = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->A - val);
			cpu_test_flag(cpu, FLAG_C, cpu->A >= val);
			break;
		}

		case CPY: {
			uint8_t val = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->Y - val);
			cpu_test_flag(cpu, FLAG_C, cpu->Y >= val);
			break;
		}

		case CPX: {
			uint8_t val = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->X - val);
			cpu_test_flag(cpu, FLAG_C, cpu->X >= val);
			break;
		}

		case TAX:
			cpu->X = cpu->A;
			cpu_eval_ZN(cpu, cpu->X);
			break;

		case TAY:
			cpu->Y = cpu->A;
			cpu_eval_ZN(cpu, cpu->Y);
			break;

		case ADC:
			cpu_adc(cpu, sys_read_cycle(nes, addr));
			break;

		case SBC:
			cpu_sbc(cpu, sys_read_cycle(nes, addr));
			break;

		case DEX:
			cpu->X -= 1;
			cpu_eval_ZN(cpu, cpu->X);
			break;

		case INX:
			cpu->X += 1;
			cpu_eval_ZN(cpu, cpu->X);
			break;

		case INY:
			cpu->Y += 1;
			cpu_eval_ZN(cpu, cpu->Y);
			break;

		case RTS:
			cpu_read_sp(cpu, nes); // Increment S
			cpu->PC = cpu_pull16(cpu, nes) + 1;
			sys_read_cycle(nes, cpu->PC); // increment PC
			break;

		case PLA:
			cpu_read_sp(cpu, nes); // Increment S
			cpu->A = cpu_pull(cpu, nes);
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case EOR:
			cpu_eor(cpu, sys_read_cycle(nes, addr));
			break;

		case LSR:
			cpu_lsr(cpu, nes, op->mode, addr);
			break;

		case ASL:
			cpu_asl(cpu, nes, op->mode, addr);
			break;

		case ROR:
			cpu_ror(cpu, nes, op->mode, addr);
			break;

		case ROL:
			cpu_rol(cpu, nes, op->mode, addr);
			break;

		case ORA:
			cpu_ora(cpu, sys_read_cycle(nes, addr));
			break;

		case STX:
			sys_write_cycle(nes, addr, cpu->X);
			break;

		case RTI:
			cpu_read_sp(cpu, nes); // Increment S
			cpu->P = (cpu_pull(cpu, nes) & 0xEF) | FLAG_U;
			cpu->PC = cpu_pull16(cpu, nes);
			break;

		case PHP:
			cpu_push(cpu, nes, cpu->P | FLAG_B | FLAG_U);
			break;

		case PLP: {
			cpu_read_sp(cpu, nes); // Increment S
			cpu->P = (cpu_pull(cpu, nes) & 0xEF) | FLAG_U;
			break;

		} case INC:
			cpu_inc(cpu, nes, addr);
			break;

		case BRK: {
			cpu->PC += 1; // BRK advances the program counter an extra byte

			cpu_push16(cpu, nes, cpu->PC);

			// NMIs can hijack BRKs here, since the CPU looks at both signals
			uint16_t vector = cpu->nmi_signal ? NMI_VECTOR : BRK_VECTOR;
			cpu_push(cpu, nes, cpu->P | FLAG_B | FLAG_U);

			SET_FLAG(cpu->P, FLAG_I);
			cpu->PC = cpu_read16(nes, vector);

			// BRK blocks any execution of real interrupts until next instruction
			cpu->irq_pending = false;
			break;

		} case TSX:
			cpu->X = cpu->SP;
			cpu_eval_ZN(cpu, cpu->X);
			break;

		case BIT: {
			uint8_t val = sys_read_cycle(nes, addr);
			cpu_test_flag(cpu, FLAG_V, (val >> 6) & 0x01);
			cpu_eval_Z(cpu, val & cpu->A);
			cpu_eval_N(cpu, val);
			break;
		}

		case NOP:
			break;


		// UNOFFICIAL
		case DOP:
			sys_read_cycle(nes, addr);
			break;

		case AAC:
			cpu_and(cpu, sys_read_cycle(nes, addr));
			cpu_test_flag(cpu, FLAG_C, cpu->A & 0x80);
			break;

		case ASR:
			cpu->A &= sys_read_cycle(nes, addr);
			cpu_test_flag(cpu, FLAG_C, cpu->A & 0x01);
			cpu->A >>= 1;
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case ARR:
			cpu->A &= sys_read_cycle(nes, addr) & cpu->A;
			cpu->A >>= 1;
			cpu->A |= GET_FLAG(cpu->P, FLAG_C) ? 0x80 : 0x00;
			cpu_eval_ZN(cpu, cpu->A);

			if ((cpu->A & 0x60) == 0x60) {
				SET_FLAG(cpu->P, FLAG_C);
				UNSET_FLAG(cpu->P, FLAG_V);

			} else if (cpu->A & 0x20) {
				SET_FLAG(cpu->P, FLAG_V);
				UNSET_FLAG(cpu->P, FLAG_C);

			} else if (cpu->A & 0x40) {
				SET_FLAG(cpu->P, FLAG_V);
				SET_FLAG(cpu->P, FLAG_C);

			} else {
				UNSET_FLAG(cpu->P, FLAG_C);
				UNSET_FLAG(cpu->P, FLAG_V);
			}
			break;

		case ATX:
			cpu->X = cpu->A = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case AXS: {
			uint8_t a = cpu->A;
			uint8_t b = sys_read_cycle(nes, addr);
			uint8_t x = cpu->X;
			cpu->X = (a & x) - b;
			cpu_eval_ZN(cpu, cpu->X);
			cpu_test_flag(cpu, FLAG_C, (a & x) - b >= 0);
			break;

		} case SLO:
			cpu_ora(cpu, cpu_asl(cpu, nes, op->mode, addr));
			break;

		case RLA:
			cpu_and(cpu, cpu_rol(cpu, nes, op->mode, addr));
			break;

		case SRE:
			cpu_eor(cpu, cpu_lsr(cpu, nes, op->mode, addr));
			break;

		case RRA:
			cpu_adc(cpu, cpu_ror(cpu, nes, op->mode, addr));
			break;

		case AAX:
			sys_write_cycle(nes, addr, cpu->A & cpu->X);
			break;

		case LAX:
			cpu->A = cpu->X = sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case DCP: {
			uint8_t val = cpu_dec(cpu, nes, addr);
			cpu_eval_ZN(cpu, cpu->A - val);
			cpu_test_flag(cpu, FLAG_C, cpu->A >= val);
			break;

		} case ISC:
			cpu_sbc(cpu, cpu_inc(cpu, nes, addr));
			break;

		case TOP:
			sys_read_cycle(nes, addr);
			break;

		case SYA:
			cpu_sxa_sya(nes, addr, cpu->Y);
			break;

		case SXA:
			cpu_sxa_sya(nes, addr, cpu->X);
			break;

		case XAA:
			cpu->A = cpu->X & sys_read_cycle(nes, addr);
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case AXA:
			cpu->X &= cpu->A;
			sys_write_cycle(nes, addr, cpu->X & 0x07);
			break;

		case LAR:
			cpu->SP &= sys_read_cycle(nes, addr);
			cpu->A = cpu->X = cpu->SP;
			cpu_eval_ZN(cpu, cpu->A);
			break;

		case XAS:
			cpu->SP = cpu->A & cpu->X;
			sys_write_cycle(nes, addr, cpu->SP & ((addr >> 8) + 1));
			break;

		default:
			NES_Log("CPU unknown opcode: %02X", code);
			return false;
	}

	return true;
}


// Interrupts

void cpu_irq(struct cpu *cpu, enum irq irq, bool enabled)
{
	if (enabled) {
		cpu->IRQ |= irq;

	} else {
		cpu->IRQ &= ~irq;
	}
}

void cpu_nmi(struct cpu *cpu, bool enabled)
{
	cpu->NMI = enabled;
}

void cpu_halt(struct cpu *cpu, bool halt)
{
	cpu->halt = halt;
}

void cpu_poll_interrupts(struct cpu *cpu)
{
	if (cpu->halt)
		return;

	cpu->irq_pending = cpu->irq_p2 || cpu->nmi_signal;
	cpu->irq_p2 = cpu->IRQ && !GET_FLAG(cpu->P, FLAG_I);
	cpu->nmi_signal = cpu->nmi_signal || (!cpu->nmi_p2 && cpu->NMI);
	cpu->nmi_p2 = cpu->NMI;
}

static void cpu_trigger_interrupt(struct cpu *cpu, NES *nes)
{
	// Internal operation
	sys_read_cycle(nes, cpu->PC);
	sys_read_cycle(nes, cpu->PC);

	cpu_push16(cpu, nes, cpu->PC);

	// Vector hijacking
	enum irq_vector vector = cpu->nmi_signal ? NMI_VECTOR : BRK_VECTOR;
	cpu_push(cpu, nes, (cpu->P & 0xEF) | FLAG_U);

	SET_FLAG(cpu->P, FLAG_I);
	cpu->PC = cpu_read16(nes, vector);

	if (vector == NMI_VECTOR)
		cpu->nmi_signal = false;
}


// Step

bool cpu_step(struct cpu *cpu, NES *nes)
{
	cpu->irq_pending = false;
	if (!cpu_exec(cpu, nes))
		return false;

	if (cpu->irq_pending)
		cpu_trigger_interrupt(cpu, nes);

	return true;
}


// Lifecycle

struct cpu *cpu_create(void)
{
	return calloc(1, sizeof(struct cpu));
}

void cpu_destroy(struct cpu **cpu)
{
	if (!cpu || !*cpu)
		return;

	free(*cpu);
	*cpu = NULL;
}

void cpu_reset(struct cpu *cpu, NES *nes, bool hard)
{
	cpu->IRQ = 0;
	cpu->irq_pending = cpu->NMI = cpu->irq_p2 = cpu->nmi_p2 =
		cpu->nmi_signal = cpu->halt = false;

	// Internal operation
	sys_read_cycle(nes, cpu->PC);
	sys_read_cycle(nes, cpu->PC);

	// Supressed writes
	sys_cycle(nes);
	sys_cycle(nes);
	sys_cycle(nes);

	cpu->PC = cpu_read16(nes, RESET_VECTOR);

	if (hard) {
		cpu->SP = 0xFD;
		cpu->A = cpu->X = cpu->Y = cpu->P = 0;
		SET_FLAG(cpu->P, FLAG_B);
		SET_FLAG(cpu->P, FLAG_U);

	} else {
		cpu->SP -= 3;
	}

	SET_FLAG(cpu->P, FLAG_I);
}


// State

size_t cpu_get_state_size(void)
{
	return sizeof(struct cpu);
}

bool cpu_set_state(struct cpu *cpu, const void *state, size_t size)
{
	if (size < cpu_get_state_size())
		return false;

	*cpu = *((const struct cpu *) state);

	return true;
}

bool cpu_get_state(struct cpu *cpu, void *state, size_t size)
{
	if (size < cpu_get_state_size())
		return false;

	memcpy(state, cpu, sizeof(struct cpu));

	return true;
}
