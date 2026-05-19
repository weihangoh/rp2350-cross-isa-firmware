#pragma once
#include <stdint.h>
#include "hardware/regs/addressmap.h"

#define WORKER_LOAD_ADDR    0x20060000u

#define CORE0_RISCV_BIT     (1u << 0)
#define CORE1_RISCV_BIT     (1u << 1)

#define OTP_ARCHSEL         (*(volatile uint32_t *)(OTP_BASE + 0x158u))
#define OTP_ARCHSEL_STATUS  (*(volatile uint32_t *)(OTP_BASE + 0x15cu))

#define ISA_ARM             0u
#define ISA_RISCV           1u

#define WORKER_MAGIC_ARM    0x41524d31u   /* "ARM1" */
#define WORKER_MAGIC_RISCV  0x52564931u   /* "RVI1" */

static inline const char *isa_name(uint32_t isa) {
    return isa == ISA_RISCV ? "RISC-V" : "ARM";
}

static inline const char *arch_name_from_bit(uint32_t value, uint32_t bit_mask) {
    return (value & bit_mask) ? "RISC-V" : "ARM";
}
