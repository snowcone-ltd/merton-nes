#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sys.h"
#include "cpu.h"

struct apu;

// IO
void apu_dma_dmc_finish(struct apu *apu, uint8_t v);
uint8_t apu_read_status(struct apu *apu, bool extended);
void apu_write(struct apu *apu, NES *nes, uint16_t addr, uint8_t v, bool extended);

// Step
void apu_step(struct apu *apu, NES *nes);
void apu_assert_irqs(struct apu *apu, struct cpu *cpu);
void apu_set_ext_output(struct apu *apu, uint8_t channel, int32_t output);
uint32_t apu_num_frames(struct apu *apu);
const int16_t *apu_pop_frames(struct apu *apu);

// Configuration
void apu_set_config(struct apu *apu, const NES_Config *cfg);

// Lifecycle
struct apu *apu_create(const NES_Config *cfg);
void apu_destroy(struct apu **apu);
void apu_reset(struct apu *apu, NES *nes, bool hard);

// State
size_t apu_get_state_size(void);
bool apu_set_state(struct apu *apu, const void *state, size_t size);
bool apu_get_state(struct apu *apu, void *state, size_t size);
