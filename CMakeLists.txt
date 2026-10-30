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

set(AK_PORT "host" CACHE STRING "Target port: host | stm32l151 | stm32g071")

project(ak_mcu_base C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# App version: edit here or pass -DAPP_VERSION=1.2.3
set(APP_VERSION "1.3.11" CACHE STRING "App version major.minor.patch")
set(APP_BUILD "0" CACHE STRING "Build number (incremented by CI)")
set(BOOT_VERSION "1.4.0" CACHE STRING "Bootloader version")

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
	${SRC}/common/sha256.c
)

set(FW_SRC
	${SRC}/services/fw/fw_image.c
	${SRC}/services/fw/fw_update.c
	${SRC}/services/fw/fw_proto.c
	${SRC}/services/fw/boot_ctrl.c
	${SRC}/services/fw/fw_pubkey.c
)

# AK_RUST_PROTO: the app splits fw_proto frames with the Rust code of ak-mcu-rust
# (crate ak-ffi -> libak_ffi.a), checked out at AK_RUST_DIR; needs cargo. The port calls
# ak_use_rust_proto(<target> <rust target triple>) on the app-side targets; the
# bootloader keeps the C state machine (fw_proto.c).
option(AK_RUST_PROTO "App splits fw_proto frames with Rust (ak-mcu-rust, crate ak-ffi)" OFF)
set(AK_RUST_DIR "${ROOT}/../ak-mcu-rust" CACHE PATH "Checkout of github.com/hohoanganh/ak-mcu-rust")
function(ak_use_rust_proto tgt rust_target)
	if(NOT AK_RUST_PROTO)
		return()
	endif()
	if(NOT EXISTS ${AK_RUST_DIR}/ak-ffi/Cargo.toml)
		message(FATAL_ERROR "AK_RUST_PROTO=ON but no ak-mcu-rust at ${AK_RUST_DIR} (-DAK_RUST_DIR=...)")
	endif()
	find_program(CARGO cargo HINTS $ENV{HOME}/.cargo/bin $ENV{CARGO_HOME}/bin)
	if(NOT CARGO)
		message(FATAL_ERROR "AK_RUST_PROTO=ON but cargo not found")
	endif()
	set(lib ${AK_RUST_DIR}/target/${rust_target}/release/libak_ffi.a)
	if(NOT TARGET ak_ffi_${rust_target})
		add_custom_target(ak_ffi_${rust_target}
			COMMAND ${CARGO} build --release -p ak-ffi --target ${rust_target}
			WORKING_DIRECTORY ${AK_RUST_DIR}
			BYPRODUCTS ${lib}
			COMMENT "cargo build -p ak-ffi --target ${rust_target}")
	endif()
	add_dependencies(${tgt} ak_ffi_${rust_target})
	target_compile_definitions(${tgt} PRIVATE AK_RUST_PROTO=1)
	target_include_directories(${tgt} PRIVATE ${AK_RUST_DIR}/ak-ffi/include)
	# libc first: memcpy/memmove from the C library, not the bigger Rust copies
	target_link_libraries(${tgt} PRIVATE c ${lib})
endfunction()

set(SYS_SRC
	${SRC}/services/sys/crash_log.c
	${SRC}/services/sys/flog.c
)

# Signed images: bootloader and app refuse images not signed with the key in
# src/services/fw/fw_pubkey.c (see fw_sign.h). AK_SIGN_KEY signs app.img.
option(AK_IMAGE_SIGN "Require ECDSA P-256 signed images" OFF)
set(AK_SIGN_KEY "${ROOT}/tools/keys/dev_priv.pem" CACHE FILEPATH "Private key (PEM) that signs app.img")
set(P256M_SRC ${ROOT}/third_party/p256-m/p256-m.c)
set(P256M_INC ${ROOT}/third_party/p256-m)

# Modbus on RS485: slave | master | gateway | none (role of the sample app);
# gateway = Modbus TCP server on the Ethernet chip (hal_tcp) forwarding to RS485
set(AK_MODBUS "slave" CACHE STRING "Modbus role of the app: slave | master | gateway | none")
set(MB_SRC
	${SRC}/services/modbus/mb_port.c
	${SRC}/services/modbus/mb_slave.c
	${SRC}/services/modbus/mb_ota.c
	${SRC}/services/modbus/mb_master.c
	${SRC}/services/modbus/mb_gateway.c
)
set(MB_INC ${SRC}/services/modbus ${ROOT}/third_party/nanomodbus)
if(AK_MODBUS STREQUAL "slave")
	set(MB_DEFS APP_MODBUS_SLAVE NMBS_CLIENT_DISABLED NMBS_STRERROR_DISABLED)
elseif(AK_MODBUS STREQUAL "master")
	set(MB_DEFS APP_MODBUS_MASTER NMBS_SERVER_DISABLED NMBS_STRERROR_DISABLED)
elseif(AK_MODBUS STREQUAL "gateway")
	set(MB_DEFS APP_MODBUS_GATEWAY NMBS_STRERROR_DISABLED)
elseif(AK_MODBUS STREQUAL "none")
	set(MB_DEFS "")
else()
	message(FATAL_ERROR "Unknown AK_MODBUS: ${AK_MODBUS}")
endif()

# What the app image carries on top of the sample app (stm32l151 only; the
# PlatformIO envs of the same name build the same thing):
#   base       the sample app alone
#   demo       AK Base Kit demo (src/demo, OLED + buttons + buzzer)
#   kit_tools  demo + SPI sniffer, nRF24 and flight control bench tools
#   remote     AK Base Kit 2.1 bench remote (src/remote), must fit 52K
#   rc_tx      remote with sticks for the ak-mcu receiver (src/rc, nRF24 in J6)
#   rc_rx      that receiver, the radio half of the future flight controller
#   fc         rc_rx + flight controller on the bench (IMU on J7, 4 motor PWM)
set(AK_APP "base" CACHE STRING "App variant: base | demo | kit_tools | remote | rc_tx | rc_rx | fc")
# src/demo is a folder of screens that grows; PlatformIO takes the whole
# folder too, so the list cannot drift between the two builds
file(GLOB DEMO_SRC CONFIGURE_DEPENDS ${SRC}/demo/*.c)
file(GLOB REMOTE_SRC CONFIGURE_DEPENDS ${SRC}/remote/*.c)
# flight control core (docs/drone-ak-mcu.md): kit_tools measures it, test_fc checks it
file(GLOB FC_SRC CONFIGURE_DEPENDS ${SRC}/fc/*.c)
# radio link of the remote (docs/drone-ak-mcu.md, P1): rc_tx / rc_rx builds, test_rc
file(GLOB RC_SRC CONFIGURE_DEPENDS ${SRC}/rc/*.c)
if(AK_APP STREQUAL "base")
	set(VARIANT_DEFS "")
elseif(AK_APP STREQUAL "demo")
	set(VARIANT_DEFS APP_KIT_DEMO)
elseif(AK_APP STREQUAL "kit_tools")
	set(VARIANT_DEFS APP_KIT_DEMO APP_KIT_SPI_SNIFF APP_RF_TEST APP_FC_BENCH)
elseif(AK_APP STREQUAL "remote")
	set(VARIANT_DEFS APP_RF_TEST APP_REMOTE KIT_LCD_SSD1306)
elseif(AK_APP STREQUAL "rc_tx")
	set(VARIANT_DEFS APP_RC_TX)
elseif(AK_APP STREQUAL "rc_rx")
	set(VARIANT_DEFS APP_RC_RX)
elseif(AK_APP STREQUAL "fc")
	# 500 Hz: the IMU is read over the bit-banged I2C of the kit (~0.75 ms)
	set(VARIANT_DEFS APP_RC_RX APP_FC FC_LOOP_HZ=500)
else()
	message(FATAL_ERROR "Unknown AK_APP: ${AK_APP}")
endif()
if(NOT AK_APP STREQUAL "base" AND NOT AK_MODBUS STREQUAL "slave")
	message(FATAL_ERROR "AK_APP=${AK_APP} runs on the Modbus slave app (AK_MODBUS=slave)")
endif()

# Kernel configuration of the firmware apps: no app of the base posts dynamic
# messages, and without them malloc/free stay out (-232 B flash, -200 B RAM).
# A project that wants them removes this (message.h default: 8).
set(APP_KERNEL_DEFS AK_DYNAMIC_MSG_POOL_SIZE=0)

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
elseif(AK_PORT STREQUAL "stm32g071")
	include(${SRC}/port/stm32g071/port.cmake)
else()
	message(FATAL_ERROR "Unknown AK_PORT: ${AK_PORT}")
endif()
