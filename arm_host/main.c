#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/gpio.h"

#include "isa_switch.h"
#include "bcdemo_shared.h"
#include "bcdemo_core.h"
#include "xisa_config.h"

#include "worker_arm_blob.h"
#include "worker_riscv_blob.h"
#include "bcdemo_program.h"

#ifdef BCDEMO_PROGRAM_EXPECTED_OUTPUT
#define BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT 1
#else
#define BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT 0
#endif


#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
#ifndef BCDEMO_PROGRAM_ENCRYPTED
#error "BCDEMO_PROTECTION_LEVEL > 0 requires an encrypted bcdemo_program.h; rerun xisa assemble --encrypted"
#endif
#else
#ifdef BCDEMO_PROGRAM_ENCRYPTED
#error "Encrypted bcdemo_program.h requires -DBCDEMO_PROTECTION_LEVEL=1 or higher"
#endif
#endif

#ifndef BCDEMO_PROGRAM_IMAGE_ID
#define BCDEMO_PROGRAM_IMAGE_ID 0u
#endif
#ifndef BCDEMO_PROGRAM_KEY_ID
#define BCDEMO_PROGRAM_KEY_ID 0u
#endif

static uint32_t g_core1_stack[1024];
static bool g_console_enabled = false;

#define LOGF(...) do { if (g_console_enabled) printf(__VA_ARGS__); } while (0)

#ifndef BCDEMO_LOG_LAUNCH_PC_OP
#define BCDEMO_LOG_LAUNCH_PC_OP 1
#endif

#ifndef BCDEMO_PRINT_INPUT_BYTES
#define BCDEMO_PRINT_INPUT_BYTES 1
#endif

#ifndef BCDEMO_PRINT_PROGRAM
#define BCDEMO_PRINT_PROGRAM 1
#endif

#ifndef BCDEMO_PRINT_EVENTS
#define BCDEMO_PRINT_EVENTS 1
#endif

#ifndef BCDEMO_HEARTBEAT_GPIO
#define BCDEMO_HEARTBEAT_GPIO 12u
#endif

#ifndef BCDEMO_PASS_GPIO
#define BCDEMO_PASS_GPIO 13u
#endif

#ifndef BCDEMO_FAIL_GPIO
#define BCDEMO_FAIL_GPIO 14u
#endif

#ifndef BCDEMO_HEARTBEAT_PULSE_MS
#define BCDEMO_HEARTBEAT_PULSE_MS 0u    /* increase to 20 if you want visible blinking */
#endif

#ifndef BCDEMO_PAUSE_EVERY_INSTRUCTIONS
#define BCDEMO_PAUSE_EVERY_INSTRUCTIONS 0u
#endif

#ifndef BCDEMO_PAUSE_MS
#define BCDEMO_PAUSE_MS 0u
#endif

#ifndef BCDEMO_PAUSE_GPIO
#define BCDEMO_PAUSE_GPIO 15u
#endif

#ifndef BCDEMO_WAIT_FOR_SERIAL_ON_START
#define BCDEMO_WAIT_FOR_SERIAL_ON_START 0
#endif

static void heartbeat_init(void) {
    gpio_init(BCDEMO_HEARTBEAT_GPIO);
    gpio_set_dir(BCDEMO_HEARTBEAT_GPIO, GPIO_OUT);
    gpio_put(BCDEMO_HEARTBEAT_GPIO, 0);
}

static void pause_gpio_init(void) {
#if BCDEMO_PAUSE_EVERY_INSTRUCTIONS > 0 && BCDEMO_PAUSE_MS > 0
    gpio_init(BCDEMO_PAUSE_GPIO);
    gpio_set_dir(BCDEMO_PAUSE_GPIO, GPIO_OUT);
    gpio_put(BCDEMO_PAUSE_GPIO, 0);
#endif
}

static void pause_after_instruction_batch(uint32_t completed_instructions) {
#if BCDEMO_PAUSE_EVERY_INSTRUCTIONS > 0 && BCDEMO_PAUSE_MS > 0
    if (completed_instructions != 0u &&
        (completed_instructions % BCDEMO_PAUSE_EVERY_INSTRUCTIONS) == 0u) {
        gpio_put(BCDEMO_PAUSE_GPIO, 1);
        sleep_ms(BCDEMO_PAUSE_MS);
        gpio_put(BCDEMO_PAUSE_GPIO, 0);
    }
#else
    (void)completed_instructions;
#endif
}

static void heartbeat_phase(uint32_t generation) {
    gpio_put(BCDEMO_HEARTBEAT_GPIO, generation & 1u);
#if BCDEMO_HEARTBEAT_PULSE_MS > 0
    sleep_ms(BCDEMO_HEARTBEAT_PULSE_MS);
#endif
}

static void heartbeat_done(void) {
    gpio_put(BCDEMO_HEARTBEAT_GPIO, 0);
}

static void status_gpio_init(void) {
    gpio_init(BCDEMO_PASS_GPIO);
    gpio_set_dir(BCDEMO_PASS_GPIO, GPIO_OUT);
    gpio_put(BCDEMO_PASS_GPIO, 0);

    gpio_init(BCDEMO_FAIL_GPIO);
    gpio_set_dir(BCDEMO_FAIL_GPIO, GPIO_OUT);
    gpio_put(BCDEMO_FAIL_GPIO, 0);
}

static void status_gpio_set_result(bool pass) {
    gpio_put(BCDEMO_PASS_GPIO, pass ? 1 : 0);
    gpio_put(BCDEMO_FAIL_GPIO, pass ? 0 : 1);
}

static void status_gpio_clear(void) {
    gpio_put(BCDEMO_PASS_GPIO, 0);
    gpio_put(BCDEMO_FAIL_GPIO, 0);
}

static void enable_console_and_wait(uint32_t timeout_ms) {
    stdio_init_all();
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!stdio_usb_connected() && !time_reached(deadline)) {
        sleep_ms(10);
    }
    sleep_ms(250);
    g_console_enabled = true;
}

static void clear_ctx(volatile bcdemo_shared_t *ctx) {
    uint8_t *p = (uint8_t *)ctx;
    for (uint32_t i = 0; i < sizeof(*ctx); ++i) p[i] = 0;
}

static void install_bcdemo_program(volatile bcdemo_shared_t *ctx) {
    ctx->program_size = BCDEMO_PROGRAM_SIZE;
    ctx->program_image_id = BCDEMO_PROGRAM_IMAGE_ID;
    ctx->program_key_id = BCDEMO_PROGRAM_KEY_ID;

#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
    uint32_t n = ctx->program_size * 4u;
    if (n > BCDEMO_PROGRAM_BYTES_MAX) n = BCDEMO_PROGRAM_BYTES_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        ctx->program_enc[i] = BCDEMO_PROGRAM_ENC[i];
    }
#else
    for (uint32_t i = 0; i < ctx->program_size; ++i) {
        ctx->program[i] = BCDEMO_PROGRAM[i];
    }
#endif
}

static void init_experiment(volatile bcdemo_shared_t *ctx) {
    clear_ctx(ctx);

    const char *msg =
        "RP2350A bytecode demo: one instruction per reset-mediated Arm/RISC-V.";

    uint32_t input_base = BCDEMO_PROGRAM_INPUT_BASE;
    uint32_t len = (uint32_t)strlen(msg);
    if (len > BCDEMO_PROGRAM_INPUT_LEN) len = BCDEMO_PROGRAM_INPUT_LEN;

    ctx->magic = BCDEMO_MAGIC;
    ctx->route_mode = BCDEMO_ROUTE_DEFAULT;
    ctx->pc = 0;
    ctx->zero_flag = 0;
    ctx->current_isa = ISA_RISCV;
    ctx->next_isa = ISA_RISCV;
    ctx->launch_request = BCDEMO_LAUNCH_CORE1;

    ctx->input_base = input_base;
    ctx->input_len = len;
    ctx->expected_output_addr = BCDEMO_PROGRAM_OUTPUT_ADDR;

    for (uint32_t i = 0; i < len; ++i) {
        ctx->memory[(input_base + i) & 0xffu] = (uint8_t)msg[i];
    }

    ctx->rng_seed_lo = 0x12345678u;
    ctx->rng_seed_hi = 0xa5e5c0deu;
    ctx->rng_state_lo = ctx->rng_seed_lo;
    ctx->rng_state_hi = ctx->rng_seed_hi;

    install_bcdemo_program(ctx);
#if BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT
    ctx->expected_output_value = (uint32_t)BCDEMO_PROGRAM_EXPECTED_OUTPUT;
#endif
    bcdemo_protection_init_context(ctx);

    watchdog_hw->scratch[BCDEMO_SCR_RUN_MAGIC_IDX] = BCDEMO_SCR_RUN_MAGIC;
}

static void stage_worker_blob(uint32_t isa) {
    if (isa == ISA_RISCV) {
        memcpy((void *)WORKER_LOAD_ADDR, worker_riscv_blob, worker_riscv_blob_LEN);
    } else {
        memcpy((void *)WORKER_LOAD_ADDR, worker_arm_blob, worker_arm_blob_LEN);
    }

    __asm volatile ("dsb sy\n"
                    "isb" ::: "memory");
}

static void set_core1_isa(uint32_t isa) {
    if (isa == ISA_RISCV) {
        OTP_ARCHSEL |= CORE1_RISCV_BIT;
    } else {
        OTP_ARCHSEL &= ~CORE1_RISCV_BIT;
    }
}

static void launch_requested_worker(volatile bcdemo_shared_t *ctx) {
    uint32_t isa = ctx->next_isa;

    heartbeat_phase(ctx->generation);
    pause_after_instruction_batch(ctx->instruction_count);

#if BCDEMO_LOG_LAUNCH_PC_OP && (BCDEMO_PROTECTION_LEVEL == BCDEMO_PROTECTION_NONE)
    LOGF("Launching core1: pc=%lu op=%s expected_isa=%s gen=%lu\n",
         (unsigned long)ctx->pc,
         bcdemo_op_name(ctx->program[ctx->pc].op),
         isa_name(isa),
         (unsigned long)ctx->generation);
#else
    LOGF("Launching core1: expected_isa=%s gen=%lu\n",
         isa_name(isa),
         (unsigned long)ctx->generation);
#endif

    set_core1_isa(isa);
    multicore_reset_core1();
    sleep_ms(10);

    stage_worker_blob(isa);

    LOGF("Worker blob staged: isa=%s len=%lu first32=0x%08lx entry32=0x%08lx\n",
         isa_name(isa),
         (unsigned long)(isa == ISA_RISCV ? worker_riscv_blob_LEN : worker_arm_blob_LEN),
         (unsigned long)(*(volatile uint32_t *)(WORKER_LOAD_ADDR + 0u)),
         (unsigned long)(*(volatile uint32_t *)(WORKER_LOAD_ADDR + 4u)));

    uint32_t status = OTP_ARCHSEL_STATUS;
    ctx->pending_archsel_status = status;
    ctx->pending_launch_isa = isa;

    LOGF("ARCHSEL_STATUS before launch: core0=%s core1=%s\n",
         arch_name_from_bit(status, CORE0_RISCV_BIT),
         arch_name_from_bit(status, CORE1_RISCV_BIT));

    if (isa == ISA_RISCV) {
        multicore_launch_core1_raw(
            (void (*)(void))(uintptr_t)(WORKER_LOAD_ADDR + 4u),
            g_core1_stack + (sizeof(g_core1_stack) / sizeof(g_core1_stack[0])),
            WORKER_LOAD_ADDR
        );
    } else {
        uint32_t reset_vector = *(volatile uint32_t *)(WORKER_LOAD_ADDR + 4u);
        multicore_launch_core1_raw(
            (void (*)(void))(uintptr_t)reset_vector,
            g_core1_stack + (sizeof(g_core1_stack) / sizeof(g_core1_stack[0])),
            WORKER_LOAD_ADDR
        );
    }

    while (1) sleep_ms(1000);
}

static void print_memory_hex(const char *label, volatile const uint8_t *p, uint32_t n) {
    printf("%s", label);
    for (uint32_t i = 0; i < n; ++i) printf("%02x", p[i]);
    printf("\n");
}

static uint32_t vm_read_u32_le(volatile const bcdemo_shared_t *ctx, uint32_t addr) {
    addr &= 0xffu;
    return ((uint32_t)ctx->memory[(addr + 0u) & 0xffu]) |
           ((uint32_t)ctx->memory[(addr + 1u) & 0xffu] << 8) |
           ((uint32_t)ctx->memory[(addr + 2u) & 0xffu] << 16) |
           ((uint32_t)ctx->memory[(addr + 3u) & 0xffu] << 24);
}

static bool verify_vm_result(volatile bcdemo_shared_t *ctx) {
    uint32_t addr = ctx->expected_output_addr & 0xffu;

    bool execution_ok =
        (ctx->error == BCDEMO_ERR_NONE) &&
        (ctx->halted != 0u);

    if (!execution_ok) {
        return false;
    }

#if BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT
    uint32_t actual = vm_read_u32_le(ctx, addr);
    return actual == (uint32_t)BCDEMO_PROGRAM_EXPECTED_OUTPUT;
#else
    /*
     * No semantic expected output was supplied.
     * Treat a clean halt as a successful execution-only run.
     */
    return true;
#endif
}

static void print_program(volatile bcdemo_shared_t *ctx) {
#if BCDEMO_PROTECTION_LEVEL == BCDEMO_PROTECTION_NONE
    printf("\nProgram:\n");
    for (uint32_t i = 0; i < ctx->program_size; ++i) {
        bcdemo_inst_t ins = ctx->program[i];
        printf("%03lu %-7s a=%3u b=%3u c=%3u\n",
               (unsigned long)i,
               bcdemo_op_name(ins.op),
               ins.a, ins.b, ins.c);
    }
#else
    (void)ctx;
    printf("\nProgram: encrypted/redacted in protected build.\n");
#endif
}

static void print_events(volatile bcdemo_shared_t *ctx) {
    printf("\nEvent log:\n");
    printf("gen pc  op      launch  worker  next_pc next_isa change regs_before regs_after  mem_before  mem_after   proof\n");

    uint32_t n = ctx->event_count;
    if (n > BCDEMO_EVENT_LOG_N) n = BCDEMO_EVENT_LOG_N;

    for (uint32_t i = 0; i < n; ++i) {
        const bcdemo_event_t *e = (const bcdemo_event_t *)&ctx->events[i];
        const char *launch_core1 = arch_name_from_bit(e->launch_archsel_status, CORE1_RISCV_BIT);

        printf("%03lu %3lu %-7s %-7s %-7s ",
               (unsigned long)e->generation,
               (unsigned long)e->pc_before,
               bcdemo_op_name(e->op),
               launch_core1,
               isa_name(e->worker_reported_isa));

        if (e->pc_after == 0xffffffffu) {
            printf("DONE    DONE     ");
        } else {
            printf("%7lu %-8s ",
                   (unsigned long)e->pc_after,
                   isa_name(e->next_isa));
        }

        printf("%6lu %08lx    %08lx  %08lx    %08lx  ",
               (unsigned long)e->did_change_isa,
               (unsigned long)e->regs_hash_before,
               (unsigned long)e->regs_hash_after,
               (unsigned long)e->mem_hash_before,
               (unsigned long)e->mem_hash_after);

        if (e->worker_reported_isa == ISA_RISCV) {
            printf("RVI1 mhartid=%08lx marchid=%08lx misa=%08lx\n",
                   (unsigned long)e->rv_mhartid,
                   (unsigned long)e->rv_marchid,
                   (unsigned long)e->rv_misa);
        } else {
            printf("ARM1 cpuid=%08lx\n", (unsigned long)e->arm_cpuid);
        }
    }
}

static void print_summary(volatile bcdemo_shared_t *ctx) {
    printf("\n=== RP2350A Bytecode Demonstrator cross-ISA bytecode run complete ===\n");
    printf("error                     = 0x%08lx\n", (unsigned long)ctx->error);
    printf("route_mode                = %s\n", bcdemo_route_mode_name(ctx->route_mode));
    printf("protection_level          = %lu\n", (unsigned long)ctx->protection_level);
    printf("program_image_id          = 0x%08lx\n", (unsigned long)ctx->program_image_id);
    printf("program_key_id            = 0x%08lx\n", (unsigned long)ctx->program_key_id);
    printf("state_mac                 = 0x%08lx%08lx\n",
           (unsigned long)ctx->state_mac_hi,
           (unsigned long)ctx->state_mac_lo);
    printf("auth_fail_count           = %lu\n", (unsigned long)ctx->auth_fail_count);
    printf("decoy_mode                = %lu\n", (unsigned long)ctx->decoy_mode);
    printf("seed                      = 0x%08lx%08lx\n",
           (unsigned long)ctx->rng_seed_hi,
           (unsigned long)ctx->rng_seed_lo);
    printf("program_size              = %lu\n", (unsigned long)ctx->program_size);
    printf("input_len                 = %lu\n", (unsigned long)ctx->input_len);
    printf("instructions executed     = %lu\n", (unsigned long)ctx->instruction_count);
    printf("reset continuations       = %lu\n", (unsigned long)ctx->reset_continuation_count);
    printf("observed ISA changes      = %lu\n", (unsigned long)ctx->isa_change_count);
    printf("same-ISA relaunches       = %lu\n", (unsigned long)ctx->same_isa_relaunch_count);
    printf("ARM stages                = %lu\n", (unsigned long)ctx->arm_stage_count);
    printf("RISC-V stages             = %lu\n", (unsigned long)ctx->riscv_stage_count);
    printf("rng draws                 = %lu\n", (unsigned long)ctx->rng_draw_count);
    printf("events stored             = %lu\n", (unsigned long)ctx->event_count);
    printf("events dropped            = %lu\n", (unsigned long)ctx->event_dropped_count);
    printf("halted                    = %lu\n", (unsigned long)ctx->halted);
    printf("last worker ISA           = %s\n", isa_name(ctx->last_worker_reported_isa));
    printf("last ARM CPUID            = 0x%08lx\n", (unsigned long)ctx->last_arm_cpuid);
    printf("last RV mhartid           = 0x%08lx\n", (unsigned long)ctx->last_rv_mhartid);
    printf("last RV marchid           = 0x%08lx\n", (unsigned long)ctx->last_rv_marchid);
    printf("last RV misa              = 0x%08lx\n", (unsigned long)ctx->last_rv_misa);

    uint32_t out_addr = ctx->expected_output_addr & 0xffu;
    uint32_t actual_u32 = vm_read_u32_le(ctx, out_addr);

    printf("actual output32           = 0x%08lx\n", (unsigned long)actual_u32);

#if BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT
    uint32_t expected_u32 = (uint32_t)BCDEMO_PROGRAM_EXPECTED_OUTPUT;
    printf("expected output32         = 0x%08lx\n", (unsigned long)expected_u32);
#endif

#if BCDEMO_PROGRAM_HAS_EXPECTED_OUTPUT
    printf("verification result       = %s\n",
           verify_vm_result(ctx) ? "PASS" : "FAIL");
#else
    printf("verification result       = %s\n",
           verify_vm_result(ctx) ? "PASS (execution only; output unchecked)" : "FAIL");
#endif

#if BCDEMO_PRINT_INPUT_BYTES
    print_memory_hex("input bytes               = ", &ctx->memory[ctx->input_base & 0xffu], ctx->input_len);
#endif

#if BCDEMO_PRINT_PROGRAM
    print_program(ctx);
#endif

#if BCDEMO_PRINT_EVENTS
    print_events(ctx);
#endif
}

int main(void) {
    heartbeat_init();
    pause_gpio_init();
    status_gpio_init();

    volatile bcdemo_shared_t *ctx = bcdemo_shared();
    bool was_wdog = watchdog_caused_reboot();
    bool fresh_start = (!was_wdog || ctx->magic != BCDEMO_MAGIC);
    bool final_state = (!fresh_start && (ctx->done || ctx->error));

    /*
     * Latch result LEDs before any USB serial wait/printf path.
     * This makes GP13/GP14 reflect completion even when no console is open.
     */
    if (final_state) {
        heartbeat_done();
        status_gpio_set_result(verify_vm_result(ctx));
    } else if (fresh_start) {
        status_gpio_clear();
    }

#if BCDEMO_DEBUG_PRINT_EACH_STAGE
    enable_console_and_wait(BCDEMO_USB_WAIT_DEBUG_MS);
#else
    if (final_state) {
        enable_console_and_wait(BCDEMO_USB_WAIT_FINAL_MS);
    } else if (fresh_start) {
#if BCDEMO_WAIT_FOR_SERIAL_ON_START
        enable_console_and_wait(BCDEMO_USB_WAIT_FINAL_MS);
#else
        g_console_enabled = false;
#endif
    }
#endif

    LOGF("\n=== RP2350A Bytecode Demonstrator reset-mediated cross-ISA bytecode VM ===\n");
    LOGF("was_wdog                  = %s\n", was_wdog ? "true" : "false");
    LOGF("ctx magic                 = 0x%08lx\n", (unsigned long)ctx->magic);
    LOGF("watchdog scratch0         = 0x%08lx\n",
         (unsigned long)watchdog_hw->scratch[BCDEMO_SCR_RUN_MAGIC_IDX]);

    if (fresh_start) {
        LOGF("Initializing new RP2350A Bytecode Demonstrator experiment.\n");
        init_experiment(ctx);
    }

    if (ctx->done || ctx->error) {
        heartbeat_done();
        /* Result LED was already latched before any USB serial wait. */
        print_summary(ctx);
        printf("\nPower-cycle or press reset to start a fresh run.\n");
        watchdog_hw->scratch[BCDEMO_SCR_RUN_MAGIC_IDX] = 0;
        while (1) sleep_ms(1000);
    }

    if (ctx->launch_request == BCDEMO_LAUNCH_CORE1) {
        launch_requested_worker(ctx);
    }

    if (!g_console_enabled) enable_console_and_wait(BCDEMO_USB_WAIT_FINAL_MS);

    printf("No launch request and not done; stopping. error=0x%08lx\n",
           (unsigned long)ctx->error);
    while (1) sleep_ms(1000);
}
