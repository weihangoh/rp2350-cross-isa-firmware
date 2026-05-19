#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "bcdemo_shared.h"

#ifndef BCDEMO_PROTECTION_LEVEL
#define BCDEMO_PROTECTION_LEVEL BCDEMO_PROTECTION_NONE
#endif

#ifndef BCDEMO_TRACE_DECODED_INSTRUCTIONS
#if BCDEMO_PROTECTION_LEVEL > BCDEMO_PROTECTION_NONE
#define BCDEMO_TRACE_DECODED_INSTRUCTIONS 0
#else
#define BCDEMO_TRACE_DECODED_INSTRUCTIONS 1
#endif
#endif

#ifndef BCDEMO_DECOY_ON_AUTH_FAIL
#define BCDEMO_DECOY_ON_AUTH_FAIL 0
#endif

const char *bcdemo_op_name(uint32_t op);
const char *bcdemo_route_mode_name(uint32_t mode);
uint32_t bcdemo_hash_regs(volatile const bcdemo_shared_t *ctx);
uint32_t bcdemo_hash_memory(volatile const bcdemo_shared_t *ctx);
uint32_t bcdemo_expected_xor_checksum(volatile const bcdemo_shared_t *ctx);
uint32_t bcdemo_program_storage_hash(volatile const bcdemo_shared_t *ctx);
uint32_t bcdemo_demo_program_key_id(void);
void bcdemo_protection_init_context(volatile bcdemo_shared_t *ctx);
bool bcdemo_state_mac_verify(volatile const bcdemo_shared_t *ctx);
void bcdemo_state_mac_update(volatile bcdemo_shared_t *ctx);
void bcdemo_secure_bzero(void *ptr, uint32_t len);

void bcdemo_worker_execute_one_instruction(volatile bcdemo_shared_t *ctx,
                                           uint32_t this_isa,
                                           uint32_t worker_magic,
                                           uint32_t arm_cpuid,
                                           uint32_t rv_mhartid,
                                           uint32_t rv_marchid,
                                           uint32_t rv_misa);
