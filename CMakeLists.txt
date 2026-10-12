# ==============================================================================
# ak-mcu-base - build system
#
#   Host (tests + simulator):
#     cmake -S . -B build/host && cmake --build build/host && ctest --test-dir build/host
#
#   STM32L151 (boot + app + OTA image):
#     cmake -S . -B build/stm32l151 -DAK_PORT=stm32l151 \
#           -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
#     cmake --build build/stm32l151
#
#   Shortcut: make test / make sim / make stm32 (Makefile in this folder).
# ==============================================================================
cmake_minimum_required(VERSION 3.16)

set(AK_PORT "host" CACHE STRING "Target port: host | stm32l151")

project(ak_mcu_base C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# App version: edit here or pass -DAPP_VERSION=1.2.3
set(APP_VERSION "1.3.8" CACHE STRING "App version major.minor.patch")
set(APP_BUILD "0" CACHE STRING "Build number (incremented by CI)")
set(BOOT_VERSION "1.3.0" CACHE STRING "Bootloader version")

string(REPLACE "." ";" _av ${APP_VERSION})
list(GET _av 0 APP_VER_MAJOR)
list(GET _av 1 APP_VER_MINOR)
list(GET _av 2 APP_VER_PATCH)
string(REPLACE "." ";" _bv ${BOOT_VERSION})
list(GET _bv 0 BOOT_VER_MAJOR)
list(GET _bv 1 BOOT_VER_MINOR)
list(GET _bv 2 BOOT_VER_PATCH)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(SRC ${ROOT}/src)

# ------------------------------------------------------------------------------
# Chip-independent components
# ------------------------------------------------------------------------------
set(KERNEL_SRC
	${SRC}/kernel/src/fsm.c
	${SRC}/kernel/src/tsm.c
	${SRC}/kernel/src/message.c
	${SRC}/kernel/src/task.c
	${SRC}/kernel/src/timer.c
)

set(COMMON_SRC
	${SRC}/common/xprintf.c
	${SRC}/common/crc.c
	${SRC}/common/ak_log.c
)

set(FW_SRC
	${SRC}/services/fw/fw_image.c
	${SRC}/services/fw/fw_update.c
	${SRC}/services/fw/fw_proto.c
	${SRC}/services/fw/boot_ctrl.c
)

set(SYS_SRC
	${SRC}/services/sys/crash_log.c
	${SRC}/services/sys/flog.c
)

# Modbus on RS485: slave | master | none (role of the sample app)
set(AK_MODBUS "slave" CACHE STRING "Modbus role of the app: slave | master | none")
set(MB_SRC
	${SRC}/services/modbus/mb_port.c
	${SRC}/services/modbus/mb_slave.c
	${SRC}/services/modbus/mb_ota.c
	${SRC}/services/modbus/mb_master.c
)
set(MB_INC ${SRC}/services/modbus ${ROOT}/third_party/nanomodbus)
if(AK_MODBUS STREQUAL "slave")
	set(MB_DEFS APP_MODBUS_SLAVE NMBS_CLIENT_DISABLED NMBS_STRERROR_DISABLED)
elseif(AK_MODBUS STREQUAL "master")
	set(MB_DEFS APP_MODBUS_MASTER NMBS_SERVER_DISABLED NMBS_STRERROR_DISABLED)
elseif(AK_MODBUS STREQUAL "none")
	set(MB_DEFS "")
else()
	message(FATAL_ERROR "Unknown AK_MODBUS: ${AK_MODBUS}")
endif()

set(BOOT_SRC
	${SRC}/boot/boot_core.c
	${SRC}/boot/boot_main.c
)

set(APP_SRC
	${SRC}/app/main.c
	${SRC}/app/task_list.c
	${SRC}/app/task_system.c
	${SRC}/app/task_console.c
	${SRC}/app/task_fw.c
	${SRC}/app/task_modbus.c
)

set(BASE_INC
	${SRC}/kernel/inc
	${SRC}/common
	${SRC}/hal
	${SRC}/services/fw
	${SRC}/services/sys
	${SRC}/boot
)

set(VERSION_DEFS
	APP_VER_MAJOR=${APP_VER_MAJOR}
	APP_VER_MINOR=${APP_VER_MINOR}
	APP_VER_PATCH=${APP_VER_PATCH}
	APP_VER_BUILD=${APP_BUILD}
	BOOT_VER_MAJOR=${BOOT_VER_MAJOR}
	BOOT_VER_MINOR=${BOOT_VER_MINOR}
	BOOT_VER_PATCH=${BOOT_VER_PATCH}
)

if(AK_PORT STREQUAL "host")
	include(${SRC}/port/host/port.cmake)
elseif(AK_PORT STREQUAL "stm32l151")
	include(${SRC}/port/stm32l151/port.cmake)
else()
	message(FATAL_ERROR "Unknown AK_PORT: ${AK_PORT}")
endif()
