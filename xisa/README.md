# xisa: RP2350 Bytecode Demonstrator Assembler

`xisa` is the assembler for the RP2350 / Raspberry Pi Pico 2 W Bytecode Demonstrator.

It has one job

> Convert `.bc` bytecode assembly files into the C header consumed by `arm_host`.

It does **not** build firmware, flash boards, capture serial logs, verify runtime traces, or generate reports.

The generated header is

```text
arm_host/bcdemo_program.h
```

## Source Tree

Expected repository layout

```text
demo_pico2w/
  arm_host/
  arm_worker/
  riscv_worker/
  common/
  xisa/
    examples/
      checksum.bc
      crc32.bc
```

Example bytecode programs

```text
xisa/examples/checksum.bc
xisa/examples/crc32.bc
```

## Install

From the `xisa/` directory

```cmd
python -m pip install -e .
```

After installation, you can either run the module form

```cmd
python -m xisa.cli ...
```

or the console command form

```cmd
xisa ...
```

## Usage from the `xisa/` Directory

From

```cmd
cd /d F:\demo_pico2w\xisa
```

Assemble plaintext bytecode

```cmd
python -m xisa.cli assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h
```

Equivalent console-command form

```cmd
xisa assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h
xisa assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h
```

Check syntax without writing a header

```cmd
python -m xisa.cli check examples\checksum.bc
```

Print the generated header to stdout

```cmd
python -m xisa.cli dump examples\checksum.bc
```

## Usage from the Repository Root

From

```cmd
cd /d F:\demo_pico2w
```

Assemble plaintext bytecode

```cmd
python -m xisa.cli assemble xisa\examples\checksum.bc -o arm_host\bcdemo_program.h
python -m xisa.cli assemble xisa\examples\crc32.bc -o arm_host\bcdemo_program.h
```

Or, if installed as a console command

```cmd
xisa assemble xisa\examples\checksum.bc -o arm_host\bcdemo_program.h
xisa assemble xisa\examples\crc32.bc -o arm_host\bcdemo_program.h
```

## Encrypted Bytecode for Protected Builds

Protection levels 1 and 2 require encrypted bytecode.

Assemble encrypted bytecode

```cmd
cd /d F:\demo_pico2w\xisa
python -m xisa.cli assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h --encrypted
```

CRC32 encrypted example

```cmd
cd /d F:\demo_pico2w\xisa
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h --encrypted
```

The default encrypted form uses the firmware demo key.

For key experiments, pass a 128-bit key as four concatenated 32-bit words

```cmd
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h --encrypted --key-hex 243f6a8885a308d313198a2e03707344
```

## Relationship to Firmware Protection Levels

Firmware protection levels are selected when building the firmware, not when running `xisa`.

Bytecode assembly must match the firmware protection level

```text
Protection level 0:
  assemble without --encrypted

Protection level 1:
  assemble with --encrypted

Protection level 2:
  assemble with --encrypted
```

Current protection levels used by the firmware

```text
0 = plaintext bytecode
1 = encrypted bytecode
2 = encrypted bytecode + authenticated state
```

Do not assemble encrypted bytecode for protection level 0.

Do not assemble plaintext bytecode for protection level 1 or 2.

The host firmware contains compile-time checks to catch this mismatch.

## Typical Firmware Build Flow

`xisa` only generates `arm_host/bcdemo_program.h`.

After assembling bytecode, build the firmware components separately

```text
1. Build arm_worker.
2. Build riscv_worker.
3. Build arm_host.
```

The host must be built last because it embeds

```text
arm_host/bcdemo_program.h
arm_host/worker_arm_blob.h
arm_host/worker_riscv_blob.h
```

Full firmware build instructions are in

```text
docs/build.md
```

## Stable Protected Demo Example

Assemble the stable protected checksum workload

```cmd
cd /d F:\demo_pico2w\xisa
python -m xisa.cli assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h --encrypted
```

Then build all firmware components with

```text
BCDEMO_PROTECTION_LEVEL=2
BCDEMO_TRACE_DECODED_INSTRUCTIONS=off
BCDEMO_ROUTE_MODE=alternate
```

See `docs/build.md` for the complete command sequence.

## CRC32 Stress Workload

Assemble CRC32 plaintext

```cmd
cd /d F:\demo_pico2w\xisa
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h
```

Assemble CRC32 encrypted

```cmd
cd /d F:\demo_pico2w\xisa
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h --encrypted
```

CRC32 is useful as a larger bytecode workload and reset-mediated execution stress test.

## Supported `.bc` Directives

```text
.input_base 100
.input_len 69
.output_addr 10
.expected_output 123
.const NAME value
```

`.expected_output` is optional.

If `.expected_output` is omitted, the generated header does not define

```text
BCDEMO_PROGRAM_EXPECTED_OUTPUT
```

In that case, the firmware should treat a clean halt as an execution-only pass.

## Labels

Labels use a trailing colon

```text
loop:
```

Branches may use labels

```text
JNZ loop
JMP loop
```

## Comments

Both semicolon and hash comments are supported

```text
; comment
# comment
```

## Instructions

```text
NOP
LOADI rX, imm16
LOAD rX, addr
STORE rX, addr
LOADR rD, rBase, rOff
STORER rS, rBase, rOff
MOV rD, rA
ADDI rD, rA, imm8
ADD rD, rA, rB
XOR rD, rA, rB
AND rD, rA, rB
SHL rD, rA, imm8
SHR rD, rA, imm8
ROL rD, rA, imm8
CMP rA, rB
JNZ label_or_addr
JMP label_or_addr
HALT
```

## Program Size Notes

The generated program must fit the firmware shared-SRAM capacity defined by

```text
BCDEMO_PROGRAM_MAX
```

This is defined in

```text
common/bcdemo_shared.h
```

The default capacity is currently 1024 instructions.

Increasing the limit requires checking the shared SRAM layout and making sure the host and both workers are built with consistent structure definitions.

## Branch Target Notes

`JNZ` and `JMP` encode 16-bit instruction targets.

Labels above instruction 255 are supported by the bytecode format.

## What `xisa` Does Not Do

`xisa` does not

```text
build firmware
flash the Pico 2 W
open serial ports
verify runtime output
capture event logs
generate UF2 files
build worker blobs
```

It only emits the C header consumed by `arm_host`.

## Quick Reference

Plaintext checksum for protection level 0

```cmd
python -m xisa.cli assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h
```

Encrypted checksum for protection level 1 or 2

```cmd
python -m xisa.cli assemble examples\checksum.bc -o ..\arm_host\bcdemo_program.h --encrypted
```

Plaintext CRC32 for protection level 0

```cmd
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h
```

Encrypted CRC32 for protection level 1 or 2

```cmd
python -m xisa.cli assemble examples\crc32.bc -o ..\arm_host\bcdemo_program.h --encrypted
```
