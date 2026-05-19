.syntax unified
.cpu cortex-m33
.thumb

.section .vectors, "a"
.align 8
.global arm_worker_vectors
arm_worker_vectors:
    .word 0x2006f000
    .word arm_worker_entry
    .word arm_worker_default_handler
    .word arm_worker_default_handler
    .word arm_worker_default_handler
    .word arm_worker_default_handler
    .word arm_worker_default_handler
    .word arm_worker_default_handler

.section .text.worker_stub, "ax"
.align 2
.global arm_worker_entry
.extern worker_c_entry
.thumb_func
arm_worker_entry:
    bl worker_c_entry

1:
    b 1b

.thumb_func
arm_worker_default_handler:
    b arm_worker_default_handler
