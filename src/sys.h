#pragma once

#include <stdint.h>

#include "nes.h"

#define SET_FLAG(reg, flag)   ((reg) |= (flag))
#define GET_FLAG(reg, flag)   ((reg) & (flag))
#define UNSET_FLAG(reg, flag) ((reg) &= ~(flag))

// IO
uint8_t sys_read(NES *nes, uint16_t addr);
void sys_write(NES *nes, uint16_t addr, uint8_t v);
void sys_dma_dmc_begin(NES *nes, uint16_t addr);

// Step
uint8_t sys_read_cycle(NES *nes, uint16_t addr);
void sys_write_cycle(NES *nes, uint16_t addr, uint8_t v);
void sys_cycle(NES *nes);
bool sys_odd_cycle(NES *nes);
