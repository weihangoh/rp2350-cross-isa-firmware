#pragma once
#include <stdint.h>
#include "isa_switch.h"

/* Shared SRAM region used for RP2350A Bytecode Demonstrator continuation state and event records. */
#define BCDEMO_SHARED_ADDR      0x20070000u
#define BCDEMO_MAGIC            0x42434431u   /* "BCD1" */
#define BCDEMO_SCR_RUN_MAGIC    0x42434450u   /* "BCDP" */
#define BCDEMO_SCR_RUN_MAGIC_IDX 0

/* Default firmware program capacity. This bounded shared-SRAM buffer may be
 * overridden with -DBCDEMO_PROGRAM_MAX=<n> if the shared memory budget allows.
 */
#ifndef BCDEMO_PROGRAM_MAX
#define BCDEMO_PROGRAM_MAX      1024u
#endif
#ifndef BCDEMO_PROGRAM_BYTES_MAX
#define BCDEMO_PROGRAM_BYTES_MAX (BCDEMO_PROGRAM_MAX * 4u)
#endif
#define BCDEMO_MEMORY_SIZE      256u
#define BCDEMO_REG_COUNT        8u
#define BCDEMO_EVENT_LOG_N      512u

#define BCDEMO_ROUTE_ALTERNATE        0u
#define BCDEMO_ROUTE_RANDOM_FREE      1u
#define BCDEMO_ROUTE_RANDOM_NO_REPEAT 2u

#define BCDEMO_LAUNCH_NONE      0u
#define BCDEMO_LAUNCH_CORE1     1u

#define BCDEMO_ERR_NONE         0u
#define BCDEMO_ERR_BAD_MAGIC    1u
#define BCDEMO_ERR_BAD_ISA      2u
#define BCDEMO_ERR_BAD_PC       3u
#define BCDEMO_ERR_BAD_OPCODE   4u
#define BCDEMO_ERR_BAD_REG      5u
#define BCDEMO_ERR_LOG_FULL     6u  /* reserved; log overflow increments event_dropped_count */
#define BCDEMO_ERR_BAD_BRANCH   7u
#define BCDEMO_ERR_AUTH         8u
#define BCDEMO_ERR_PROTECTION   9u

#define BCDEMO_PROTECTION_NONE       0u
#define BCDEMO_PROTECTION_ENCRYPTED  1u
#define BCDEMO_PROTECTION_MAC        2u
#define BCDEMO_PROTECTION_FAILCLOSED 3u

/* Fixed-width 4-byte instruction format.
 * Meaning of a/b/c depends on op.
 */
typedef struct {
    uint8_t op;
    uint8_t a;
    uint8_t b;
    uint8_t c;
} bcdemo_inst_t;

enum {
    BCDEMO_OP_NOP    = 0x00,
    BCDEMO_OP_LOADI  = 0x01, /* a=rX, imm16=(b | c<<8) */
    BCDEMO_OP_LOAD   = 0x02, /* a=rX, b=direct byte address */
    BCDEMO_OP_STORE  = 0x03, /* a=rX, b=direct byte address */
    BCDEMO_OP_LOADR  = 0x04, /* a=rD, b=rBase, c=rOff; mem[(rBase+rOff)&255] */
    BCDEMO_OP_STORER = 0x05, /* a=rS, b=rBase, c=rOff; mem[(rBase+rOff)&255]=rS */
    BCDEMO_OP_MOV    = 0x06, /* a=rD, b=rA */
    BCDEMO_OP_ADDI   = 0x07, /* a=rD, b=rA, c=imm8 */
    BCDEMO_OP_ADD    = 0x08, /* a=rD, b=rA, c=rB */
    BCDEMO_OP_XOR    = 0x09, /* a=rD, b=rA, c=rB */
    BCDEMO_OP_AND    = 0x0a, /* a=rD, b=rA, c=rB */
    BCDEMO_OP_SHL    = 0x0b, /* a=rD, b=rA, c=shift */
    BCDEMO_OP_SHR    = 0x0c, /* a=rD, b=rA, c=shift */
    BCDEMO_OP_ROL    = 0x0d, /* a=rD, b=rA, c=rotate */
    BCDEMO_OP_CMP    = 0x0e, /* a=rA, b=rB; zero_flag=(rA==rB) */
    BCDEMO_OP_JNZ    = 0x0f, /* target pc = a | (b<<8); jump if zero_flag == 0 */
    BCDEMO_OP_JMP    = 0x10, /* target pc = a | (b<<8) */
    BCDEMO_OP_HALT   = 0x11  /* stop VM */
};

#define BCDEMO_NOP()                 ((bcdemo_inst_t){BCDEMO_OP_NOP,   0, 0, 0})
#define BCDEMO_LOADI(r, imm)         ((bcdemo_inst_t){BCDEMO_OP_LOADI, (r), (uint8_t)((imm) & 0xffu), (uint8_t)(((imm) >> 8) & 0xffu)})
#define BCDEMO_LOAD(r, addr)         ((bcdemo_inst_t){BCDEMO_OP_LOAD,  (r), (uint8_t)(addr), 0})
#define BCDEMO_STORE(r, addr)        ((bcdemo_inst_t){BCDEMO_OP_STORE, (r), (uint8_t)(addr), 0})
#define BCDEMO_LOADR(rd, rb, ro)     ((bcdemo_inst_t){BCDEMO_OP_LOADR, (rd), (rb), (ro)})
#define BCDEMO_STORER(rs, rb, ro)    ((bcdemo_inst_t){BCDEMO_OP_STORER,(rs), (rb), (ro)})
#define BCDEMO_MOV(rd, ra)           ((bcdemo_inst_t){BCDEMO_OP_MOV,   (rd), (ra), 0})
#define BCDEMO_ADDI(rd, ra, imm)     ((bcdemo_inst_t){BCDEMO_OP_ADDI,  (rd), (ra), (uint8_t)(imm)})
#define BCDEMO_ADD(rd, ra, rb)       ((bcdemo_inst_t){BCDEMO_OP_ADD,   (rd), (ra), (rb)})
#define BCDEMO_XOR(rd, ra, rb)       ((bcdemo_inst_t){BCDEMO_OP_XOR,   (rd), (ra), (rb)})
#define BCDEMO_AND(rd, ra, rb)       ((bcdemo_inst_t){BCDEMO_OP_AND,   (rd), (ra), (rb)})
#define BCDEMO_SHL(rd, ra, imm)      ((bcdemo_inst_t){BCDEMO_OP_SHL,   (rd), (ra), (uint8_t)(imm)})
#define BCDEMO_SHR(rd, ra, imm)      ((bcdemo_inst_t){BCDEMO_OP_SHR,   (rd), (ra), (uint8_t)(imm)})
#define BCDEMO_ROL(rd, ra, imm)      ((bcdemo_inst_t){BCDEMO_OP_ROL,   (rd), (ra), (uint8_t)(imm)})
#define BCDEMO_CMP(ra, rb)           ((bcdemo_inst_t){BCDEMO_OP_CMP,   (ra), (rb), 0})
#define BCDEMO_JNZ(target)           ((bcdemo_inst_t){BCDEMO_OP_JNZ,   (uint8_t)((target) & 0xffu), (uint8_t)(((target) >> 8) & 0xffu), 0})
#define BCDEMO_JMP(target)           ((bcdemo_inst_t){BCDEMO_OP_JMP,   (uint8_t)((target) & 0xffu), (uint8_t)(((target) >> 8) & 0xffu), 0})
#define BCDEMO_HALT()                ((bcdemo_inst_t){BCDEMO_OP_HALT,  0, 0, 0})

typedef struct {
    uint32_t generation;
    uint32_t pc_before;
    uint32_t pc_after;
    uint32_t this_isa;
    uint32_t op;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t next_isa;
    uint32_t did_change_isa;
    uint32_t regs_hash_before;
    uint32_t regs_hash_after;
    uint32_t mem_hash_before;
    uint32_t mem_hash_after;

    uint32_t launch_archsel_status;
    uint32_t launch_requested_isa;
    uint32_t worker_magic;
    uint32_t worker_reported_isa;
    uint32_t arm_cpuid;
    uint32_t rv_mhartid;
    uint32_t rv_marchid;
    uint32_t rv_misa;
} bcdemo_event_t;

typedef struct {
    volatile uint32_t magic;

    volatile uint32_t launch_request;
    volatile uint32_t current_isa;
    volatile uint32_t next_isa;
    volatile uint32_t route_mode;

    volatile uint32_t pc;
    volatile uint32_t program_size;
    volatile uint32_t zero_flag;
    volatile uint32_t halted;
    volatile uint32_t done;
    volatile uint32_t error;

    volatile uint32_t generation;
    volatile uint32_t instruction_count;
    volatile uint32_t reset_continuation_count;
    volatile uint32_t isa_change_count;
    volatile uint32_t same_isa_relaunch_count;
    volatile uint32_t arm_stage_count;
    volatile uint32_t riscv_stage_count;

    volatile uint32_t rng_seed_lo;
    volatile uint32_t rng_seed_hi;
    volatile uint32_t rng_state_lo;
    volatile uint32_t rng_state_hi;
    volatile uint32_t rng_draw_count;

    volatile uint32_t protection_level;
    volatile uint32_t program_image_id;
    volatile uint32_t program_key_id;
    volatile uint32_t state_mac_lo;
    volatile uint32_t state_mac_hi;
    volatile uint32_t auth_fail_count;
    volatile uint32_t decoy_mode;

    volatile uint32_t regs[BCDEMO_REG_COUNT];
    volatile uint8_t memory[BCDEMO_MEMORY_SIZE];
    union {
        bcdemo_inst_t program[BCDEMO_PROGRAM_MAX];
        uint8_t program_enc[BCDEMO_PROGRAM_BYTES_MAX];
    };

    volatile uint32_t expected_output_addr;
    volatile uint32_t expected_output_value;
    volatile uint32_t input_base;
    volatile uint32_t input_len;

    volatile uint32_t pending_archsel_status;
    volatile uint32_t pending_launch_isa;

    volatile uint32_t last_worker_magic;
    volatile uint32_t last_worker_reported_isa;
    volatile uint32_t last_arm_cpuid;
    volatile uint32_t last_rv_mhartid;
    volatile uint32_t last_rv_marchid;
    volatile uint32_t last_rv_misa;

    volatile uint32_t event_count;
    volatile uint32_t event_dropped_count;
    bcdemo_event_t events[BCDEMO_EVENT_LOG_N];
} bcdemo_shared_t;

static inline volatile bcdemo_shared_t *bcdemo_shared(void) {
    return (volatile bcdemo_shared_t *)BCDEMO_SHARED_ADDR;
}
