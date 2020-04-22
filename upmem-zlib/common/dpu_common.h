#ifndef DPU_COMMON_H
#define DPU_COMMON_H

#include <mram.h>

#define MRAM_MEMORY_SIZE (1 << 20) // 1MB

uint8_t __mram_noinit mram_memory[MRAM_MEMORY_SIZE];
uint32_t mram_memory_index = 1;

#endif
