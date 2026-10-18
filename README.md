# RP2350 Cross-ISA Firmware Demonstrator

**Reset-mediated bytecode execution across Arm Cortex-M33 and RISC-V Hazard3 on Raspberry Pi Pico 2 W (RP2350).**

Embedded-firmware research prototype. The repository contains source code for a bytecode VM whose worker executes one *virtual* instruction per stage, then requests a watchdog reboot. Its logical execution state is stored in an SRAM region that the host reuse on reboot. The next stage may use a different native ISA on core 1.

This project provides an implementation for studying what an analyst observes when a VM's logical execution spans watchdog resets and Arm/RISC-V worker phases.

## How it works

1. **Arm host (core 0):** On a fresh start, initializes a bytecode workload and its logical state at `BCDEMO_SHARED_ADDR` (`0x20070000`). On watchdog restart, it checks for the retained state's magic value and decides whether to resume or initialize.
2. **Worker launch:** The host reads the requested ISA, sets the core-1 architecture selection (`OTP_ARCHSEL`), resets core 1, copies the corresponding Arm or RISC-V worker binary to `WORKER_LOAD_ADDR` (`0x20060000`), and launches core 1.
3. **One VM instruction:** The selected worker calls the shared `bcdemo_worker_execute_one_instruction()` implementation, which fetches and executes **one 4-byte VM instruction** and updates the logical state. The same C interpreter is compiled separately for the two native ISAs.
4. **Next stage:** The interpreter selects `next_isa` according to the configured routing mode. The worker requests a device watchdog reboot using `watchdog_reboot(0, 0, 1)`. If more VM instructions remain, the next host invocation stages and launches the next worker. Before requesting the watchdog reboot, the worker also updates the core-1 architecture-selection setting for the next stage.
5. **Completion:** On VM halt or error, the worker still requests the final reboot; the host then reports the result and stops rather than launching another worker.

```text
            Raspberry Pi Pico 2 W / RP2350
    +--------------------------------------------+
    | Arm host (core 0)                          |
    | Fresh start or watchdog restart            |<--------+
    | Initialize or recover retained VM state    |         |
    +---------------------+----------------------+         |
                          |                                |
                          |                                |
                +-------------------+                      |
                | VM halted or      |                      |
                | error detected?   |                      |
                +----+---------+----+                      |
                     |         |                           |
    Report   <------Yes        No                          |
    results                    |                           |
    and stop                   |                           |
                 +-------------+-------------+             |
                 | Select ISA (OTP_ARCHSEL)  |             |
                 | Stage Arm/RISC-V worker   |             |
                 | Launch core 1             |             |
                 +-------------+-------------+             |
                               |                           |
                               |                           |
                 +-------------+-------------+             |
                 | Arm or RISC-V (core 1)    |             |
                 | Execute one VM instruction|             |
                 | Update retained VM state  |             |
                 | Select next ISA           |             |
                 +-------------+-------------+             |
                               |                           |
                               |                           |
                 +-------------+-------------+             |
                 | Request watchdog reboot   |             |
                 | (including final stage)   |             |
                 +-------------+-------------+             |
                               |                           |
                               +---------------------------+
```

## What is included

- **XISA assembler** (`xisa/xisa/`): translates `.bc` source into `arm_host/bcdemo_program.h`, optionally with encrypted bytecode storage. Two demo workloads are included, `xisa/examples/checksum.bc` (a 69-byte XOR checksum) and `xisa/examples/crc32.bc` (a longer bitwise CRC32 workload).
- **Three ISA-routing selections:** `alternate`, `random-free`, and `no-repeat`. **In the current code, `no-repeat` and `alternate` both deterministically select the opposite ISA.** `random-free` makes an unforced pseudo-random choice, with deterministic demonstration seeds set by the host.
- **Retained VM context:** program counter, VM registers, 256-byte VM memory, routing and pseudorandom state, instrumentation counters, and an event-record array, at the shared SRAM address.
- **Optional trace detail:** decoded per-instruction event fields can be enabled or disabled at build time; even with decoded fields disabled, the event records retain stage/ISA-related information.
- **Experimental protection modes:** 0 = plaintext, 1 = bytecode encrypted using a demonstration key, 2 = encrypted plus VM-state MAC. Source and component CMake configurations also define **level 3** (fail-closed handling on authentication failure).

## Build a checksum demonstration

### Prerequisites

- Raspberry Pi Pico 2 W (RP2350) and a USB connection
- Raspberry Pi Pico SDK, configured for RP2350 and including the required Arm and RISC-V toolchains
- CMake **3.20 or newer**, Ninja, Python 3, and the SDK's other toolchain dependencies
- `PICO_SDK_PATH` environment variable pointing to the Pico SDK

The following **root CMake path is defined in `CMakeLists.txt`**. It assembles the selected `.bc` file, builds both worker image headers, and then builds the host firmware. For the project's documented manual component-by-component build, see [docs/build.md](docs/build.md).

On **Windows Command Prompt**, from the repository root:

```bat
set PICO_SDK_PATH=C:\path\to\pico-sdk
cmake -S . -B build -G Ninja -DBCDEMO_PROGRAM=checksum -DBCDEMO_PROGRAM_ENCRYPTED=ON -DBCDEMO_PROTECTION_LEVEL=2 -DBCDEMO_ROUTE_MODE=alternate -DBCDEMO_TRACE_DECODED_INSTRUCTIONS=off
cmake --build build --target firmware
```

The host CMake configuration names its output:

```text
arm_host/build/arm_host.uf2
```

The build order is important because the host includes generated `bcdemo_program.h`, `worker_arm_blob.h`, and `worker_riscv_blob.h`. These files are **not checked in** and must be generated by the build.

### Expected output

`docs/build.md` identifies the level-2 checksum/`alternate` configuration as its stable demonstration and documents:

```text
verification result       = PASS
actual output32           = 0x00000044
expected output32         = 0x00000044
protection_level          = 2
```

The expected `0x44` is consistent with the checked-in 69-byte host input and the XOR loop in `checksum.bc`.

The host code defines GP12 as an activity output, GP13 as PASS, and GP14 as FAIL (and GP15 as an optional pause indication). These are GPIO signals; attach appropriate external indicators if you wish to observe them. The usual configuration suppresses routine intermediate serial output and prints the completion summary following the final watchdog restart, subject to USB serial connection.

### Inspecting execution

The host's instrumented output reports VM instruction counts, reset-continuation counts, Arm/RISC-V stage counts, selected ISA changes, final output verification, and recorded event data. The worker captures Arm CPUID or RISC-V CSR identifiers for its event records and diagnostic observations.

Decoded VM operation/PC and state hashes are controlled by `BCDEMO_TRACE_DECODED_INSTRUCTIONS`:

- `off` — redacts decoded VM instruction fields in stored event records.
- `on` — records instruction/opcode and before/after state hashes. **This deliberately exposes VM-level execution details.**
- `auto` (component CMake default) — controlled by `bcdemo_core.h`: enabled at protection level 0, disabled at protected levels.

For a coherent trace, compile the **host and both worker binaries** with the same trace configuration. The root CMake configuration passes this setting to each component. Reconfigure and rebuild before switching the setting.

## Configuration caveats

- **Routing:** `alternate` is the documented stable demonstration route. `no-repeat` is also deterministic alternation in this source snapshot; `random-free` is deterministic from the fixed host seeds (`0x12345678`, `0xa5e5c0de`) and can select the same ISA in consecutive stages.
- **Bytecode encryption:** the assembler's `--encrypted` output must match firmware protection level 1, 2, or 3. Plaintext assembly is required for level 0. The host contains compile-time checks for encrypted/plaintext mismatches.
- **Protection is experimental:** the source embeds a demonstration key in the firmware, which does not provide confidentiality against someone who can extract the image. The level-2/3 MAC is a project-specific construction rather than an independently reviewed integrity protocol. Hardware-backed key storage is not implemented in the shown key provider.
- **Longer workloads:** the build guide treats encrypted CRC32 at levels 1 and 2 as a stress test that may stall, not the primary stable demonstration.
- **Logging:** `BCDEMO_EVENT_LOG_N` is 512. Additional events are counted as dropped, while VM execution continues (unless some other error ends it).
- **Memory limits:** the current shared structure specifies 8 VM registers, 256 bytes of VM memory, 4-byte VM instructions, and a default capacity of 1,024 program instructions. Changing shared structure sizes requires checking the RP2350 SRAM layout and rebuilding all firmware components consistently.
- **Rebuilding:** changing protection or shared runtime code requires rebuilding host and both workers; changing only a bytecode program generally requires regenerating the program header and rebuilding the host (see `docs/build.md`).

## Repository structure

| Path | Role |
| --- | --- |
| `arm_host/` | Host-side startup, continuation, ISA selection, worker staging, and final reporting |
| `arm_worker/` | Arm worker and entry stub |
| `riscv_worker/` | RISC-V worker and entry stub |
| `common/` | Shared VM structures/engine, ISA-register definitions, bytecode protection code |
| `xisa/` | `.bc` assembler and checked-in checksum/CRC32 workloads |
| `docs/build.md` | Maintainer's manual build and expected-output instructions |
| `scripts/` | SWD/SRAM-related helper scripts |
| `CMakeLists.txt` | Top-level source-defined build orchestration |

## Further reading

- [Manual builds and expected results](docs/build.md)
- [XISA assembler](xisa/README.md)
- [Host orchestration](arm_host/main.c)
- [Shared VM implementation](common/bcdemo_core.c)
- [Shared context](common/bcdemo_shared.h)
- [ISA selection and worker address](common/isa_switch.h)

## License

Licensed under the Apache License, Version 2.0.

SPDX-License-Identifier: Apache-2.0

See [LICENSE](LICENSE) for the full license text.
