cmake_minimum_required(VERSION 3.20)

set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Pico platform")
set(PICO_BOARD pico2_w CACHE STRING "Pico board")
set(BCDEMO_ROUTE_MODE "random-free" CACHE STRING "RP2350A Bytecode Demonstrator routing mode: random-free, no-repeat, alternate")

set(BCDEMO_OUTPUT_PROFILE "instrumented" CACHE STRING "RP2350A Bytecode Demonstrator output profile: instrumented, program-blind")
set(BCDEMO_PROTECTION_LEVEL "0" CACHE STRING "RP2350A Bytecode Demonstrator protection level: 0 plaintext, 1 encrypted, 2 encrypted+MAC, 3 fail-closed")
set(BCDEMO_TRACE_DECODED_INSTRUCTIONS "auto" CACHE STRING "RP2350A Bytecode Demonstrator decoded event trace: auto, on, off")
set(BCDEMO_DECOY_ON_AUTH_FAIL "0" CACHE STRING "RP2350A Bytecode Demonstrator protected mode: enter harmless decoy state on auth failure")
set(BCDEMO_HEARTBEAT_PULSE_MS "0" CACHE STRING "RP2350A Bytecode Demonstrator heartbeat pulse delay in ms")
set(BCDEMO_PAUSE_EVERY_INSTRUCTIONS "0" CACHE STRING "RP2350A Bytecode Demonstrator: pause core0 relaunch after every N completed bytecode instructions")
set(BCDEMO_PAUSE_MS "0" CACHE STRING "RP2350A Bytecode Demonstrator: pause duration in ms for periodic relaunch pacing")
set(BCDEMO_PAUSE_GPIO "15" CACHE STRING "RP2350A Bytecode Demonstrator: GPIO lit while periodic relaunch pause is active")
set(BCDEMO_DEBUG_PRINT_EACH_STAGE "0" CACHE STRING "RP2350A Bytecode Demonstrator: reopen USB serial and print on every reset stage")
set(BCDEMO_WAIT_FOR_SERIAL_ON_START "0" CACHE STRING "RP2350A Bytecode Demonstrator: wait for USB serial before starting a fresh run")

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(arm_host C CXX ASM)
pico_sdk_init()

add_executable(arm_host
    main.c
    ../common/bcdemo_core.c
)

target_include_directories(arm_host PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../common
)

target_compile_definitions(arm_host PRIVATE
    BCDEMO_PROTECTION_LEVEL=${BCDEMO_PROTECTION_LEVEL}
    BCDEMO_DECOY_ON_AUTH_FAIL=${BCDEMO_DECOY_ON_AUTH_FAIL}
    BCDEMO_HEARTBEAT_PULSE_MS=${BCDEMO_HEARTBEAT_PULSE_MS}
    BCDEMO_PAUSE_EVERY_INSTRUCTIONS=${BCDEMO_PAUSE_EVERY_INSTRUCTIONS}
    BCDEMO_PAUSE_MS=${BCDEMO_PAUSE_MS}
    BCDEMO_PAUSE_GPIO=${BCDEMO_PAUSE_GPIO}
    BCDEMO_DEBUG_PRINT_EACH_STAGE=${BCDEMO_DEBUG_PRINT_EACH_STAGE}
    BCDEMO_WAIT_FOR_SERIAL_ON_START=${BCDEMO_WAIT_FOR_SERIAL_ON_START}
)

if(BCDEMO_TRACE_DECODED_INSTRUCTIONS STREQUAL "on")
    target_compile_definitions(arm_host PRIVATE BCDEMO_TRACE_DECODED_INSTRUCTIONS=1)
elseif(BCDEMO_TRACE_DECODED_INSTRUCTIONS STREQUAL "off")
    target_compile_definitions(arm_host PRIVATE BCDEMO_TRACE_DECODED_INSTRUCTIONS=0)
endif()

if(BCDEMO_ROUTE_MODE STREQUAL "no-repeat")
    target_compile_definitions(arm_host PRIVATE BCDEMO_ROUTE_DEFAULT=BCDEMO_ROUTE_RANDOM_NO_REPEAT)
elseif(BCDEMO_ROUTE_MODE STREQUAL "alternate")
    target_compile_definitions(arm_host PRIVATE BCDEMO_ROUTE_DEFAULT=BCDEMO_ROUTE_ALTERNATE)
else()
    target_compile_definitions(arm_host PRIVATE BCDEMO_ROUTE_DEFAULT=BCDEMO_ROUTE_RANDOM_FREE)
endif()

if(BCDEMO_OUTPUT_PROFILE STREQUAL "program-blind")
    target_compile_definitions(arm_host PRIVATE
        BCDEMO_LOG_LAUNCH_PC_OP=0
        BCDEMO_PRINT_INPUT_BYTES=0
        BCDEMO_PRINT_PROGRAM=0
        BCDEMO_PRINT_EVENTS=0
    )
else()
    target_compile_definitions(arm_host PRIVATE
        BCDEMO_LOG_LAUNCH_PC_OP=1
        BCDEMO_PRINT_INPUT_BYTES=1
        BCDEMO_PRINT_PROGRAM=1
        BCDEMO_PRINT_EVENTS=1
    )
endif()

target_link_libraries(arm_host
    pico_stdlib
    pico_multicore
    hardware_watchdog
    hardware_gpio
)

pico_enable_stdio_usb(arm_host 1)
pico_enable_stdio_uart(arm_host 0)

pico_add_extra_outputs(arm_host)
