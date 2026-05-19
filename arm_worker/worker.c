#include <stdint.h>
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "bcdemo_shared.h"
#include "bcdemo_core.h"
#include "isa_switch.h"

#ifndef WORKER_ISA
#error WORKER_ISA must be ISA_ARM or ISA_RISCV
#endif

static void request_next_reboot(volatile bcdemo_shared_t *ctx) {
    if (ctx->done || ctx->error) {
        /* Final reset returns control to core0 host for printing. */
    } else if (ctx->next_isa == ISA_RISCV) {
        OTP_ARCHSEL |= CORE1_RISCV_BIT;
    } else {
        OTP_ARCHSEL &= ~CORE1_RISCV_BIT;
    }

    watchdog_hw->scratch[BCDEMO_SCR_RUN_MAGIC_IDX] = BCDEMO_SCR_RUN_MAGIC;
    watchdog_start_tick(12);
    watchdog_reboot(0, 0, 1);

    while (1) {
        __asm volatile ("" ::: "memory");
    }
}

#if WORKER_ISA == ISA_ARM
#define SCB_CPUID_ADDR 0xE000ED00u
static inline uint32_t read_arm_cpuid(void) {
    return *(volatile uint32_t *)SCB_CPUID_ADDR;
}
#endif

void worker_c_entry(void) {
    volatile bcdemo_shared_t *ctx = bcdemo_shared();

#if WORKER_ISA == ISA_ARM
    uint32_t worker_magic = WORKER_MAGIC_ARM;
    uint32_t arm_cpuid = read_arm_cpuid();
    uint32_t rv_mhartid = 0;
    uint32_t rv_marchid = 0;
    uint32_t rv_misa = 0;
#else
    uint32_t worker_magic = WORKER_MAGIC_RISCV;
    uint32_t arm_cpuid = 0;
    uint32_t rv_mhartid = 0;
    uint32_t rv_marchid = 0;
    uint32_t rv_misa = 0;
#endif

    bcdemo_worker_execute_one_instruction(ctx,
                                          WORKER_ISA,
                                          worker_magic,
                                          arm_cpuid,
                                          rv_mhartid,
                                          rv_marchid,
                                          rv_misa);

    request_next_reboot(ctx);
}
