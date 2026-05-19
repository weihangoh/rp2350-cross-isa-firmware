#include "bcdemo_core.h"

#if defined(__GNUC__)
#define BCDEMO_MAYBE_UNUSED __attribute__((unused))
#else
#define BCDEMO_MAYBE_UNUSED
#endif

const char *bcdemo_op_name(uint32_t op) {
    switch (op) {
    case BCDEMO_OP_NOP:    return "NOP";
    case BCDEMO_OP_LOADI:  return "LOADI";
    case BCDEMO_OP_LOAD:   return "LOAD";
    case BCDEMO_OP_STORE:  return "STORE";
    case BCDEMO_OP_LOADR:  return "LOADR";
    case BCDEMO_OP_STORER: return "STORER";
    case BCDEMO_OP_MOV:    return "MOV";
    case BCDEMO_OP_ADDI:   return "ADDI";
    case BCDEMO_OP_ADD:    return "ADD";
    case BCDEMO_OP_XOR:    return "XOR";
    case BCDEMO_OP_AND:    return "AND";
    case BCDEMO_OP_SHL:    return "SHL";
    case BCDEMO_OP_SHR:    return "SHR";
    case BCDEMO_OP_ROL:    return "ROL";
    case BCDEMO_OP_CMP:    return "CMP";
    case BCDEMO_OP_JNZ:    return "JNZ";
    case BCDEMO_OP_JMP:    return "JMP";
    case BCDEMO_OP_HALT:   return "HALT";
    default:            return "?";
    }
}

const char *bcdemo_route_mode_name(uint32_t mode) {
    switch (mode) {
    case BCDEMO_ROUTE_ALTERNATE:        return "ALTERNATE";
    case BCDEMO_ROUTE_RANDOM_FREE:      return "RANDOM_FREE";
    case BCDEMO_ROUTE_RANDOM_NO_REPEAT: return "RANDOM_NO_REPEAT";
    default:                            return "UNKNOWN";
    }
}

static uint32_t fnv1a_mix_u32(uint32_t h, uint32_t v) {
    for (uint32_t i = 0; i < 4; ++i) {
        h ^= (uint8_t)(v >> (8u * i));
        h *= 16777619u;
    }
    return h;
}

uint32_t bcdemo_hash_regs(volatile const bcdemo_shared_t *ctx) {
    uint32_t h = 2166136261u;
    h = fnv1a_mix_u32(h, ctx->pc);
    h = fnv1a_mix_u32(h, ctx->zero_flag);
    for (uint32_t i = 0; i < BCDEMO_REG_COUNT; ++i) {
        h = fnv1a_mix_u32(h, ctx->regs[i]);
    }
    return h;
}

uint32_t bcdemo_hash_memory(volatile const bcdemo_shared_t *ctx) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < BCDEMO_MEMORY_SIZE; ++i) {
        h ^= ctx->memory[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t bcdemo_expected_xor_checksum(volatile const bcdemo_shared_t *ctx) {
    uint32_t x = 0;
    uint32_t base = ctx->input_base & 0xffu;
    uint32_t n = ctx->input_len;
    for (uint32_t i = 0; i < n; ++i) {
        x ^= ctx->memory[(base + i) & 0xffu];
    }
    return x & 0xffu;
}


void bcdemo_secure_bzero(void *ptr, uint32_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) *p++ = 0;
}

static inline void bcdemo_compiler_barrier(void) {
    __asm volatile ("" ::: "memory");
}

static void store_le32_u8(uint8_t b[4], uint32_t v) {
    b[0] = (uint8_t)(v & 0xffu);
    b[1] = (uint8_t)((v >> 8) & 0xffu);
    b[2] = (uint8_t)((v >> 16) & 0xffu);
    b[3] = (uint8_t)((v >> 24) & 0xffu);
}

/*
 * Demo key provider.
 *
 * Architecture note: the protected-mode code calls through this provider so a
 * later hardware-backed design can replace the compiled demo key with OTP,
 * secure-element, provisioning, or other device-unique material.  The default
 * key below is intentionally only a demonstration key; if it remains compiled
 * into the UF2, encrypted bytecode provides analysis friction rather than true
 * confidentiality.
 */
#ifndef BCDEMO_DEMO_KEY0
#define BCDEMO_DEMO_KEY0 0x243f6a88u
#endif
#ifndef BCDEMO_DEMO_KEY1
#define BCDEMO_DEMO_KEY1 0x85a308d3u
#endif
#ifndef BCDEMO_DEMO_KEY2
#define BCDEMO_DEMO_KEY2 0x13198a2eu
#endif
#ifndef BCDEMO_DEMO_KEY3
#define BCDEMO_DEMO_KEY3 0x03707344u
#endif

static void bcdemo_get_program_key(uint32_t key[4]) {
    key[0] = BCDEMO_DEMO_KEY0;
    key[1] = BCDEMO_DEMO_KEY1;
    key[2] = BCDEMO_DEMO_KEY2;
    key[3] = BCDEMO_DEMO_KEY3;
}

uint32_t bcdemo_demo_program_key_id(void) {
    uint32_t key[4];
    bcdemo_get_program_key(key);
    uint32_t h = 2166136261u;
    h = fnv1a_mix_u32(h, key[0]);
    h = fnv1a_mix_u32(h, key[1]);
    h = fnv1a_mix_u32(h, key[2]);
    h = fnv1a_mix_u32(h, key[3]);
    bcdemo_secure_bzero(key, sizeof key);
    return h;
}

static void xtea_encrypt_block(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0];
    uint32_t v1 = v[1];
    uint32_t sum = 0;
    const uint32_t delta = 0x9e3779b9u;

    for (uint32_t i = 0; i < 32u; ++i) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3u]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3u]);
    }

    v[0] = v0;
    v[1] = v1;
}

static void BCDEMO_MAYBE_UNUSED bcdemo_instruction_keystream(uint32_t pc, uint32_t program_image_id, uint8_t out[4]) {
    uint32_t key[4];
    bcdemo_get_program_key(key);

    uint32_t block[2];
    block[0] = 0x42434445u ^ program_image_id;  /* "BCDE" domain */
    block[1] = 0x31584953u ^ pc;                /* "SIX1" domain */
    xtea_encrypt_block(block, key);

    store_le32_u8(out, block[0]);
    bcdemo_secure_bzero(block, sizeof block);
    bcdemo_secure_bzero(key, sizeof key);
}

uint32_t bcdemo_program_storage_hash(volatile const bcdemo_shared_t *ctx) {
    uint32_t h = 2166136261u;
    h = fnv1a_mix_u32(h, ctx->program_size);
    h = fnv1a_mix_u32(h, ctx->protection_level);
    h = fnv1a_mix_u32(h, ctx->program_image_id);

#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
    uint32_t n = ctx->program_size * 4u;
    if (n > BCDEMO_PROGRAM_BYTES_MAX) n = BCDEMO_PROGRAM_BYTES_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        h ^= ctx->program_enc[i];
        h *= 16777619u;
    }
#else
    for (uint32_t i = 0; i < ctx->program_size && i < BCDEMO_PROGRAM_MAX; ++i) {
        h = fnv1a_mix_u32(h, ctx->program[i].op);
        h = fnv1a_mix_u32(h, ctx->program[i].a);
        h = fnv1a_mix_u32(h, ctx->program[i].b);
        h = fnv1a_mix_u32(h, ctx->program[i].c);
    }
#endif

    return h;
}

typedef struct {
    uint32_t c0;
    uint32_t c1;
    uint32_t pending;
    uint32_t have_pending;
    uint32_t word_count;
} bcdemo_mac_t;

static void bcdemo_mac_init(bcdemo_mac_t *m) {
    uint32_t key[4];
    bcdemo_get_program_key(key);
    m->c0 = 0x42434443u ^ key[0]; /* "BCDC" domain */
    m->c1 = 0x4d414331u ^ key[1]; /* "MAC1" domain */
    m->pending = 0;
    m->have_pending = 0;
    m->word_count = 0;
    bcdemo_secure_bzero(key, sizeof key);
}

static void bcdemo_mac_absorb_pair(bcdemo_mac_t *m, uint32_t w0, uint32_t w1) {
    uint32_t key[4];
    bcdemo_get_program_key(key);
    uint32_t block[2];
    block[0] = m->c0 ^ w0;
    block[1] = m->c1 ^ w1;
    xtea_encrypt_block(block, key);
    m->c0 = block[0];
    m->c1 = block[1];
    bcdemo_secure_bzero(block, sizeof block);
    bcdemo_secure_bzero(key, sizeof key);
}

static void bcdemo_mac_u32(bcdemo_mac_t *m, uint32_t v) {
    if (m->have_pending) {
        bcdemo_mac_absorb_pair(m, m->pending, v);
        m->have_pending = 0;
        m->pending = 0;
    } else {
        m->pending = v;
        m->have_pending = 1;
    }
    m->word_count++;
}

static void bcdemo_mac_finish(bcdemo_mac_t *m, uint32_t *lo, uint32_t *hi) {
    if (m->have_pending) {
        bcdemo_mac_absorb_pair(m, m->pending, 0x80000000u ^ m->word_count);
        m->have_pending = 0;
        m->pending = 0;
    }
    bcdemo_mac_absorb_pair(m, 0x42454e44u, m->word_count); /* "BEND" */
    *lo = m->c0;
    *hi = m->c1;
}

static void BCDEMO_MAYBE_UNUSED bcdemo_compute_state_mac(volatile const bcdemo_shared_t *ctx, uint32_t *lo, uint32_t *hi) {
    bcdemo_mac_t m;
    bcdemo_mac_init(&m);

    bcdemo_mac_u32(&m, BCDEMO_MAGIC);
    bcdemo_mac_u32(&m, ctx->protection_level);
    bcdemo_mac_u32(&m, ctx->program_size);
    bcdemo_mac_u32(&m, bcdemo_program_storage_hash(ctx));
    bcdemo_mac_u32(&m, ctx->program_image_id);
    bcdemo_mac_u32(&m, ctx->program_key_id);
    bcdemo_mac_u32(&m, ctx->pc);
    bcdemo_mac_u32(&m, ctx->current_isa);
    bcdemo_mac_u32(&m, ctx->next_isa);
    bcdemo_mac_u32(&m, ctx->route_mode);
    bcdemo_mac_u32(&m, ctx->zero_flag);
    bcdemo_mac_u32(&m, ctx->halted);
    bcdemo_mac_u32(&m, ctx->done);
    bcdemo_mac_u32(&m, ctx->error);
    bcdemo_mac_u32(&m, ctx->generation);
    bcdemo_mac_u32(&m, ctx->instruction_count);
    bcdemo_mac_u32(&m, ctx->reset_continuation_count);
    bcdemo_mac_u32(&m, ctx->isa_change_count);
    bcdemo_mac_u32(&m, ctx->same_isa_relaunch_count);
    bcdemo_mac_u32(&m, ctx->arm_stage_count);
    bcdemo_mac_u32(&m, ctx->riscv_stage_count);
    bcdemo_mac_u32(&m, ctx->rng_seed_lo);
    bcdemo_mac_u32(&m, ctx->rng_seed_hi);
    bcdemo_mac_u32(&m, ctx->rng_state_lo);
    bcdemo_mac_u32(&m, ctx->rng_state_hi);
    bcdemo_mac_u32(&m, ctx->rng_draw_count);
    bcdemo_mac_u32(&m, ctx->expected_output_addr);
    bcdemo_mac_u32(&m, ctx->expected_output_value);
    bcdemo_mac_u32(&m, ctx->input_base);
    bcdemo_mac_u32(&m, ctx->input_len);

    for (uint32_t i = 0; i < BCDEMO_REG_COUNT; ++i) {
        bcdemo_mac_u32(&m, ctx->regs[i]);
    }

    for (uint32_t i = 0; i < BCDEMO_MEMORY_SIZE; i += 4u) {
        uint32_t w = ((uint32_t)ctx->memory[(i + 0u) & 0xffu]) |
                     ((uint32_t)ctx->memory[(i + 1u) & 0xffu] << 8) |
                     ((uint32_t)ctx->memory[(i + 2u) & 0xffu] << 16) |
                     ((uint32_t)ctx->memory[(i + 3u) & 0xffu] << 24);
        bcdemo_mac_u32(&m, w);
    }

    bcdemo_mac_finish(&m, lo, hi);
    bcdemo_secure_bzero(&m, sizeof m);
}

bool bcdemo_state_mac_verify(volatile const bcdemo_shared_t *ctx) {
#if BCDEMO_PROTECTION_LEVEL >= BCDEMO_PROTECTION_MAC
    uint32_t lo = 0;
    uint32_t hi = 0;
    bcdemo_compute_state_mac(ctx, &lo, &hi);
    return lo == ctx->state_mac_lo && hi == ctx->state_mac_hi;
#else
    (void)ctx;
    return true;
#endif
}

void bcdemo_state_mac_update(volatile bcdemo_shared_t *ctx) {
#if BCDEMO_PROTECTION_LEVEL >= BCDEMO_PROTECTION_MAC
    uint32_t lo = 0;
    uint32_t hi = 0;
    ctx->state_mac_lo = 0;
    ctx->state_mac_hi = 0;
    bcdemo_compute_state_mac(ctx, &lo, &hi);
    ctx->state_mac_lo = lo;
    ctx->state_mac_hi = hi;
#else
    (void)ctx;
#endif
}

void bcdemo_protection_init_context(volatile bcdemo_shared_t *ctx) {
    ctx->protection_level = BCDEMO_PROTECTION_LEVEL;
    if (ctx->program_key_id == 0u) {
        ctx->program_key_id = bcdemo_demo_program_key_id();
    }
    ctx->auth_fail_count = 0;
    ctx->decoy_mode = 0;
    ctx->state_mac_lo = 0;
    ctx->state_mac_hi = 0;
    bcdemo_state_mac_update(ctx);
}

static bcdemo_inst_t bcdemo_fetch_instruction(volatile const bcdemo_shared_t *ctx, uint32_t pc) {
#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
    uint8_t ks[4];
    uint8_t pt[4];
    uint32_t off = pc * 4u;
    bcdemo_instruction_keystream(pc, ctx->program_image_id, ks);
    for (uint32_t i = 0; i < 4u; ++i) {
        pt[i] = ctx->program_enc[off + i] ^ ks[i];
    }
    bcdemo_inst_t ins = { pt[0], pt[1], pt[2], pt[3] };
    bcdemo_secure_bzero(ks, sizeof ks);
    bcdemo_secure_bzero(pt, sizeof pt);
    return ins;
#else
    return ctx->program[pc];
#endif
}

static void BCDEMO_MAYBE_UNUSED bcdemo_fail_auth(volatile bcdemo_shared_t *ctx) {
    ctx->auth_fail_count++;
#if BCDEMO_PROTECTION_LEVEL >= BCDEMO_PROTECTION_FAILCLOSED
    for (uint32_t i = 0; i < BCDEMO_REG_COUNT; ++i) ctx->regs[i] = 0;
    for (uint32_t i = 0; i < BCDEMO_MEMORY_SIZE; ++i) ctx->memory[i] = 0;
#if BCDEMO_DECOY_ON_AUTH_FAIL
    ctx->decoy_mode = 1;
    uint32_t addr = ctx->expected_output_addr & 0xffu;
    ctx->memory[(addr + 0u) & 0xffu] = 0x01u;
    ctx->memory[(addr + 1u) & 0xffu] = 0xdeu;
    ctx->memory[(addr + 2u) & 0xffu] = 0xc0u;
    ctx->memory[(addr + 3u) & 0xffu] = 0xdeu;
#endif
#endif
    ctx->error = BCDEMO_ERR_AUTH;
    ctx->done = 1u;
    ctx->halted = 0u;
    ctx->launch_request = BCDEMO_LAUNCH_NONE;
}

static uint64_t ctx_rng64(volatile bcdemo_shared_t *ctx) {
    uint64_t x = ((uint64_t)ctx->rng_state_hi << 32) | ctx->rng_state_lo;
    x += 0x9E3779B97F4A7C15ull;

    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);

    ctx->rng_state_lo = (uint32_t)x;
    ctx->rng_state_hi = (uint32_t)(x >> 32);
    ctx->rng_draw_count++;
    return z;
}

static uint32_t choose_next_isa(volatile bcdemo_shared_t *ctx, uint32_t this_isa) {
    switch (ctx->route_mode) {
    case BCDEMO_ROUTE_ALTERNATE:
        return this_isa == ISA_ARM ? ISA_RISCV : ISA_ARM;
    case BCDEMO_ROUTE_RANDOM_FREE:
        return (ctx_rng64(ctx) & 1u) ? ISA_RISCV : ISA_ARM;
    case BCDEMO_ROUTE_RANDOM_NO_REPEAT:
        return this_isa == ISA_ARM ? ISA_RISCV : ISA_ARM;
    default:
        ctx->error = 0xBAD00001u;
        return ISA_ARM;
    }
}

static int valid_reg(uint32_t r) {
    return r < BCDEMO_REG_COUNT;
}

static uint32_t rol32(uint32_t x, uint32_t n) {
    n &= 31u;
    return n ? ((x << n) | (x >> (32u - n))) : x;
}

void bcdemo_worker_execute_one_instruction(volatile bcdemo_shared_t *ctx,
                                           uint32_t this_isa,
                                           uint32_t worker_magic,
                                           uint32_t arm_cpuid,
                                           uint32_t rv_mhartid,
                                           uint32_t rv_marchid,
                                           uint32_t rv_misa) {
    if (ctx->magic != BCDEMO_MAGIC) {
        ctx->error = BCDEMO_ERR_BAD_MAGIC;
        ctx->done = 1;
        return;
    }

#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
    if (ctx->protection_level != BCDEMO_PROTECTION_LEVEL ||
        ctx->program_key_id != bcdemo_demo_program_key_id()) {
        bcdemo_fail_auth(ctx);
        return;
    }
#endif

#if BCDEMO_PROTECTION_LEVEL >= BCDEMO_PROTECTION_MAC
    if (!bcdemo_state_mac_verify(ctx)) {
        bcdemo_fail_auth(ctx);
        return;
    }
#endif

    if (ctx->current_isa != this_isa) {
        ctx->error = BCDEMO_ERR_BAD_ISA;
        ctx->done = 1;
        return;
    }

    ctx->last_worker_magic = worker_magic;
    ctx->last_worker_reported_isa = this_isa;
    ctx->last_arm_cpuid = arm_cpuid;
    ctx->last_rv_mhartid = rv_mhartid;
    ctx->last_rv_marchid = rv_marchid;
    ctx->last_rv_misa = rv_misa;

    uint32_t pc_before = ctx->pc;
    uint32_t generation_for_event = ctx->generation;

    if (pc_before >= ctx->program_size || pc_before >= BCDEMO_PROGRAM_MAX) {
        ctx->error = BCDEMO_ERR_BAD_PC;
        ctx->done = 1;
        return;
    }

    bcdemo_inst_t ins = bcdemo_fetch_instruction(ctx, pc_before);
    bcdemo_compiler_barrier();
    uint32_t before_regs = bcdemo_hash_regs(ctx);
    uint32_t before_mem = bcdemo_hash_memory(ctx);
    uint32_t next_pc = pc_before + 1u;
    uint32_t has_next = 1u;

    switch (ins.op) {
    case BCDEMO_OP_NOP:
        break;

    case BCDEMO_OP_LOADI:
        if (!valid_reg(ins.a)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = (uint32_t)ins.b | ((uint32_t)ins.c << 8);
        break;

    case BCDEMO_OP_LOAD:
        if (!valid_reg(ins.a)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->memory[ins.b];
        break;

    case BCDEMO_OP_STORE:
        if (!valid_reg(ins.a)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->memory[ins.b] = (uint8_t)(ctx->regs[ins.a] & 0xffu);
        break;

    case BCDEMO_OP_LOADR:
        if (!valid_reg(ins.a) || !valid_reg(ins.b) || !valid_reg(ins.c)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->memory[(ctx->regs[ins.b] + ctx->regs[ins.c]) & 0xffu];
        break;

    case BCDEMO_OP_STORER:
        if (!valid_reg(ins.a) || !valid_reg(ins.b) || !valid_reg(ins.c)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->memory[(ctx->regs[ins.b] + ctx->regs[ins.c]) & 0xffu] = (uint8_t)(ctx->regs[ins.a] & 0xffu);
        break;

    case BCDEMO_OP_MOV:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b];
        break;

    case BCDEMO_OP_ADDI:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] + ins.c;
        break;

    case BCDEMO_OP_ADD:
        if (!valid_reg(ins.a) || !valid_reg(ins.b) || !valid_reg(ins.c)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] + ctx->regs[ins.c];
        break;

    case BCDEMO_OP_XOR:
        if (!valid_reg(ins.a) || !valid_reg(ins.b) || !valid_reg(ins.c)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] ^ ctx->regs[ins.c];
        break;

    case BCDEMO_OP_AND:
        if (!valid_reg(ins.a) || !valid_reg(ins.b) || !valid_reg(ins.c)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] & ctx->regs[ins.c];
        break;

    case BCDEMO_OP_SHL:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] << (ins.c & 31u);
        break;

    case BCDEMO_OP_SHR:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = ctx->regs[ins.b] >> (ins.c & 31u);
        break;

    case BCDEMO_OP_ROL:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->regs[ins.a] = rol32(ctx->regs[ins.b], ins.c);
        break;

    case BCDEMO_OP_CMP:
        if (!valid_reg(ins.a) || !valid_reg(ins.b)) { ctx->error = BCDEMO_ERR_BAD_REG; break; }
        ctx->zero_flag = (ctx->regs[ins.a] == ctx->regs[ins.b]) ? 1u : 0u;
        break;

    case BCDEMO_OP_JNZ: {
        uint32_t target = (uint32_t)ins.a | ((uint32_t)ins.b << 8);
        if (ctx->zero_flag == 0u) next_pc = target;
        break;
    }

    case BCDEMO_OP_JMP: {
        uint32_t target = (uint32_t)ins.a | ((uint32_t)ins.b << 8);
        next_pc = target;
        break;
    }

    case BCDEMO_OP_HALT:
        has_next = 0u;
        ctx->halted = 1u;
        ctx->done = 1u;
        ctx->launch_request = BCDEMO_LAUNCH_NONE;
        break;

    default:
        ctx->error = BCDEMO_ERR_BAD_OPCODE;
        break;
    }

    if (ctx->error != BCDEMO_ERR_NONE) {
        has_next = 0u;
        ctx->done = 1u;
        ctx->launch_request = BCDEMO_LAUNCH_NONE;
    }

    if (has_next && next_pc >= ctx->program_size) {
        ctx->error = BCDEMO_ERR_BAD_BRANCH;
        has_next = 0u;
        ctx->done = 1u;
        ctx->launch_request = BCDEMO_LAUNCH_NONE;
    }

    uint32_t after_regs = bcdemo_hash_regs(ctx);
    uint32_t after_mem = bcdemo_hash_memory(ctx);
#if !BCDEMO_TRACE_DECODED_INSTRUCTIONS
    (void)before_regs;
    (void)before_mem;
    (void)after_regs;
    (void)after_mem;
#endif

    uint32_t next_isa = this_isa;
    if (has_next) next_isa = choose_next_isa(ctx, this_isa);

    uint32_t ev = ctx->event_count;
    if (ev < BCDEMO_EVENT_LOG_N) {
        ctx->events[ev].generation = generation_for_event;
#if BCDEMO_TRACE_DECODED_INSTRUCTIONS
        ctx->events[ev].pc_before = pc_before;
        ctx->events[ev].pc_after = has_next ? next_pc : 0xffffffffu;
        ctx->events[ev].op = ins.op;
        ctx->events[ev].a = ins.a;
        ctx->events[ev].b = ins.b;
        ctx->events[ev].c = ins.c;
        ctx->events[ev].regs_hash_before = before_regs;
        ctx->events[ev].regs_hash_after = after_regs;
        ctx->events[ev].mem_hash_before = before_mem;
        ctx->events[ev].mem_hash_after = after_mem;
#else
        ctx->events[ev].pc_before = 0xffffffffu;
        ctx->events[ev].pc_after = has_next ? 0xfffffffeu : 0xffffffffu;
        ctx->events[ev].op = 0xffffffffu;
        ctx->events[ev].a = 0xffffffffu;
        ctx->events[ev].b = 0xffffffffu;
        ctx->events[ev].c = 0xffffffffu;
        ctx->events[ev].regs_hash_before = 0u;
        ctx->events[ev].regs_hash_after = 0u;
        ctx->events[ev].mem_hash_before = 0u;
        ctx->events[ev].mem_hash_after = 0u;
#endif
        ctx->events[ev].this_isa = this_isa;
        ctx->events[ev].next_isa = has_next ? next_isa : 0xffffffffu;
        ctx->events[ev].did_change_isa = has_next && (next_isa != this_isa);
        ctx->events[ev].launch_archsel_status = ctx->pending_archsel_status;
        ctx->events[ev].launch_requested_isa = ctx->pending_launch_isa;
        ctx->events[ev].worker_magic = worker_magic;
        ctx->events[ev].worker_reported_isa = this_isa;
        ctx->events[ev].arm_cpuid = arm_cpuid;
        ctx->events[ev].rv_mhartid = rv_mhartid;
        ctx->events[ev].rv_marchid = rv_marchid;
        ctx->events[ev].rv_misa = rv_misa;
        ctx->event_count = ev + 1u;
    } else {
        /* Long RP2350A Bytecode Demonstrator programs such as bitwise CRC32 can execute far more
         * instructions than we want to retain in SRAM.  Do not stop the VM
         * just because the trace buffer is full.  Keep executing and count
         * how many event rows were dropped.
         */
        ctx->event_dropped_count++;
    }

    if (this_isa == ISA_ARM) ctx->arm_stage_count++;
    else ctx->riscv_stage_count++;

    if (has_next) {
        if (next_isa != this_isa) ctx->isa_change_count++;
        else ctx->same_isa_relaunch_count++;

        ctx->pc = next_pc;
        ctx->next_isa = next_isa;
        ctx->current_isa = next_isa;
        ctx->launch_request = BCDEMO_LAUNCH_CORE1;
    }

    bcdemo_secure_bzero(&ins, sizeof ins);

    ctx->instruction_count++;
    ctx->reset_continuation_count++;
    ctx->generation++;

    bcdemo_state_mac_update(ctx);
}
