cmake_minimum_required(VERSION 3.20)

set(PICO_PLATFORM rp2350-riscv CACHE STRING "Pico platform")
set(PICO_BOARD pico2_w CACHE STRING "Pico board")
set(BCDEMO_PROTECTION_LEVEL "0" CACHE STRING "RP2350A Bytecode Demonstrator protection level: 0 plaintext, 1 encrypted, 2 encrypted+MAC, 3 fail-closed")
set(BCDEMO_TRACE_DECODED_INSTRUCTIONS "auto" CACHE STRING "RP2350A Bytecode Demonstrator decoded event trace: auto, on, off")
set(BCDEMO_DECOY_ON_AUTH_FAIL "0" CACHE STRING "RP2350A Bytecode Demonstrator protected mode: enter harmless decoy state on auth failure")

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(riscv_worker C ASM)
pico_sdk_init()

add_executable(riscv_worker
    worker_stub.S
    worker.c
    ../common/bcdemo_core.c
)

target_include_directories(riscv_worker PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../common
)

target_compile_definitions(riscv_worker PRIVATE
    WORKER_ISA=ISA_RISCV
    BCDEMO_PROTECTION_LEVEL=${BCDEMO_PROTECTION_LEVEL}
    BCDEMO_DECOY_ON_AUTH_FAIL=${BCDEMO_DECOY_ON_AUTH_FAIL}
)

if(BCDEMO_TRACE_DECODED_INSTRUCTIONS STREQUAL "on")
    target_compile_definitions(riscv_worker PRIVATE BCDEMO_TRACE_DECODED_INSTRUCTIONS=1)
elseif(BCDEMO_TRACE_DECODED_INSTRUCTIONS STREQUAL "off")
    target_compile_definitions(riscv_worker PRIVATE BCDEMO_TRACE_DECODED_INSTRUCTIONS=0)
endif()

target_compile_options(riscv_worker PRIVATE
    -ffreestanding
    -fno-builtin
    -fdata-sections
    -ffunction-sections
)

target_link_options(riscv_worker PRIVATE
    -nostdlib
    -Wl,-T,${CMAKE_CURRENT_LIST_DIR}/worker.ld
    -Wl,--gc-sections
    -Wl,-e,riscv_worker_entry
)

target_link_libraries(riscv_worker
    hardware_watchdog
)

set(WORKER_BIN ${CMAKE_CURRENT_BINARY_DIR}/riscv_worker.bin)
set(WORKER_HEADER ${CMAKE_CURRENT_LIST_DIR}/../arm_host/worker_riscv_blob.h)

add_custom_command(
    OUTPUT ${WORKER_BIN}
    COMMAND ${CMAKE_OBJCOPY} -O binary
            ${CMAKE_CURRENT_BINARY_DIR}/riscv_worker.elf
            ${WORKER_BIN}
    DEPENDS riscv_worker
    VERBATIM
)

add_custom_command(
    OUTPUT ${WORKER_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${WORKER_BIN}
            -DOUTPUT=${WORKER_HEADER}
            -DSYMBOL=worker_riscv_blob
            -P ${CMAKE_CURRENT_LIST_DIR}/../common/bin2h.cmake
    DEPENDS ${WORKER_BIN} ${CMAKE_CURRENT_LIST_DIR}/../common/bin2h.cmake
    VERBATIM
)

add_custom_target(worker_riscv_blob ALL
    DEPENDS ${WORKER_HEADER}
)
