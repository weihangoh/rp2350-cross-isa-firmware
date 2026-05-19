.section .text.worker_stub,"ax",@progbits
.global riscv_worker_trap
.global riscv_worker_entry
.extern worker_c_entry

riscv_worker_trap:
1:  j 1b

riscv_worker_entry:
    j worker_c_entry
