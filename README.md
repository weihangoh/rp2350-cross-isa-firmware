# RP2350 Cross-ISA Firmware Demonstrator

Research prototype for reset-mediated Arm/RISC-V firmware execution on Raspberry Pi RP2350 / Pico 2 W.

## Status

Private research repository.  Prepared for Black Hat Arsenal submission/demo development.

## Components

- `arm_host/` - Arm host firmware for staging and launching workers
- `arm_worker/` - Arm worker firmware
- `riscv_worker/` - RISC-V worker firmware
- `common/` - shared bytecode interpreter / protection / runtime code
- `xisa/` - bytecode assembler and examples

## Generated Files

The following files are generated and are not committed

- `arm_host/bcdemo_program.h`
- `arm_host/worker_arm_blob.h`
- `arm_host/worker_riscv_blob.h`

Regenerate them by building the workers and assembling the bytecode examples.

## Build

See `docs/build.md`.

## License

This project is licensed under the Apache License 2.0.
