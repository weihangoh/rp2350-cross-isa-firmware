cmake_minimum_required(VERSION 3.20)

project(demo_pico2w NONE)

include(ExternalProject)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BCDEMO_PROGRAM "checksum" CACHE STRING "Bytecode example name without .bc extension")
set(BCDEMO_PROGRAM_ENCRYPTED "ON" CACHE BOOL "Assemble bytecode as encrypted program")

set(BCDEMO_PROTECTION_LEVEL "2" CACHE STRING "Protection level: 0=plain, 1=encrypted, 2=encrypted+MAC")
set(BCDEMO_TRACE_DECODED_INSTRUCTIONS "off" CACHE STRING "Trace decoded instructions: auto, on, off")
set(BCDEMO_OUTPUT_PROFILE "instrumented" CACHE STRING "Host output profile: instrumented or program-blind")
set(BCDEMO_ROUTE_MODE "alternate" CACHE STRING "Host route mode: alternate, random-free, no-repeat")

set(BCDEMO_WAIT_FOR_SERIAL_ON_START "0" CACHE STRING "Wait for USB serial before starting fresh run")
set(BCDEMO_PAUSE_EVERY_INSTRUCTIONS "0" CACHE STRING "Pause after every N completed bytecode instructions")
set(BCDEMO_PAUSE_MS "0" CACHE STRING "Pause duration in ms")
set(BCDEMO_PAUSE_GPIO "15" CACHE STRING "GPIO asserted while pause is active")

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(BCDEMO_PROGRAM_ENCRYPTED)
    set(XISA_ENCRYPTED_FLAG --encrypted)
else()
    set(XISA_ENCRYPTED_FLAG)
endif()

add_custom_target(assemble_program
    COMMAND "${Python3_EXECUTABLE}" -m xisa.cli assemble "examples/${BCDEMO_PROGRAM}.bc" -o "../arm_host/bcdemo_program.h" ${XISA_ENCRYPTED_FLAG}
    WORKING_DIRECTORY "${ROOT_DIR}/xisa"
    DEPENDS "${ROOT_DIR}/xisa/examples/${BCDEMO_PROGRAM}.bc"
    COMMENT "Assembling xisa/examples/${BCDEMO_PROGRAM}.bc"
    VERBATIM
)

ExternalProject_Add(arm_worker_project
    SOURCE_DIR "${ROOT_DIR}/arm_worker"
    BINARY_DIR "${ROOT_DIR}/arm_worker/build"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -DBCDEMO_PROTECTION_LEVEL=${BCDEMO_PROTECTION_LEVEL}
        -DBCDEMO_TRACE_DECODED_INSTRUCTIONS=${BCDEMO_TRACE_DECODED_INSTRUCTIONS}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --target worker_arm_blob
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
)

ExternalProject_Add(riscv_worker_project
    SOURCE_DIR "${ROOT_DIR}/riscv_worker"
    BINARY_DIR "${ROOT_DIR}/riscv_worker/build"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -DBCDEMO_PROTECTION_LEVEL=${BCDEMO_PROTECTION_LEVEL}
        -DBCDEMO_TRACE_DECODED_INSTRUCTIONS=${BCDEMO_TRACE_DECODED_INSTRUCTIONS}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --target worker_riscv_blob
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
)

ExternalProject_Add(arm_host_project
    SOURCE_DIR "${ROOT_DIR}/arm_host"
    BINARY_DIR "${ROOT_DIR}/arm_host/build"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        -DBCDEMO_PROTECTION_LEVEL=${BCDEMO_PROTECTION_LEVEL}
        -DBCDEMO_TRACE_DECODED_INSTRUCTIONS=${BCDEMO_TRACE_DECODED_INSTRUCTIONS}
        -DBCDEMO_OUTPUT_PROFILE=${BCDEMO_OUTPUT_PROFILE}
        -DBCDEMO_ROUTE_MODE=${BCDEMO_ROUTE_MODE}
        -DBCDEMO_WAIT_FOR_SERIAL_ON_START=${BCDEMO_WAIT_FOR_SERIAL_ON_START}
        -DBCDEMO_PAUSE_EVERY_INSTRUCTIONS=${BCDEMO_PAUSE_EVERY_INSTRUCTIONS}
        -DBCDEMO_PAUSE_MS=${BCDEMO_PAUSE_MS}
        -DBCDEMO_PAUSE_GPIO=${BCDEMO_PAUSE_GPIO}
    DEPENDS
        assemble_program
        arm_worker_project
        riscv_worker_project
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
)

add_custom_target(firmware
    DEPENDS arm_host_project
)