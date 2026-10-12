# Port STM32L151CB: builds boot.elf/.bin, app.elf/.bin and app.img (OTA image).
set(PORT_DIR ${SRC}/port/stm32l151)

# SPL + CMSIS of the STM32L1: the part the build uses is in third_party/stm32l1
set(STM32L1_LIB_DIR ${ROOT}/third_party/stm32l1
	CACHE PATH "Directory containing STM32L1xx_StdPeriph_Driver and CMSIS")
if(NOT EXISTS ${STM32L1_LIB_DIR}/STM32L1xx_StdPeriph_Driver)
	message(FATAL_ERROR "SPL not found: ${STM32L1_LIB_DIR} (set -DSTM32L1_LIB_DIR=...)")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Where OTA images are staged: external W25Qxx (APP 116K) or internal flash (APP 58K).
set(AK_STAGING "external" CACHE STRING "OTA staging location: external | internal")
if(AK_STAGING STREQUAL "external")
	set(STAGING_DEFS PORT_STAGING_EXTERNAL=1)
	set(APP_PART_SIZE 0x1D000)
	set(STAGING_SRC ${SRC}/port/stm32l151/spi_nor.c)
elseif(AK_STAGING STREQUAL "internal")
	set(STAGING_DEFS PORT_STAGING_EXTERNAL=0)
	set(APP_PART_SIZE 0xE800)
	set(STAGING_SRC "")
else()
	message(FATAL_ERROR "Unknown AK_STAGING: ${AK_STAGING}")
endif()
if(AK_IMAGE_SIGN)
	# 16K bootloader: APP moves to 0x08004000 and shrinks by 4K
	set(BOOT_SIZE 0x4000)
	if(AK_STAGING STREQUAL "external")
		set(APP_PART_SIZE 0x1C000)
	else()
		set(APP_PART_SIZE 0xE000)
	endif()
	set(SIGN_DEFS PORT_IMAGE_SIGN=1 FW_IMAGE_SIGN=1)
	set(SIGN_ARGS --sign-key ${AK_SIGN_KEY})
else()
	set(BOOT_SIZE 0x3000)
	set(SIGN_DEFS "")
	set(SIGN_ARGS "")
endif()
message(STATUS "STM32L151 staging: ${AK_STAGING}, signed images: ${AK_IMAGE_SIGN} (BOOT ${BOOT_SIZE}, APP partition ${APP_PART_SIZE})")

set(SPL_DIR ${STM32L1_LIB_DIR}/STM32L1xx_StdPeriph_Driver)
set(SPL_SRC
	${SPL_DIR}/src/misc.c
	${SPL_DIR}/src/stm32l1xx_rcc.c
	${SPL_DIR}/src/stm32l1xx_gpio.c
	${SPL_DIR}/src/stm32l1xx_usart.c
	${SPL_DIR}/src/stm32l1xx_flash.c
	${SPL_DIR}/src/stm32l1xx_iwdg.c
	${SPL_DIR}/src/stm32l1xx_spi.c
)

set(PORT_INC
	${PORT_DIR}
	${SPL_DIR}/inc
	${STM32L1_LIB_DIR}/CMSIS/Device/ST/STM32L1xx/Include
	${STM32L1_LIB_DIR}/CMSIS/Include
)

set(MCU_FLAGS -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)

# Link-time optimization: about 14% smaller boot and app.
option(AK_LTO "Build boot/app with -flto" ON)
if(AK_LTO)
	list(APPEND MCU_FLAGS -flto)
endif()

# AK Base Kit with an nRF24 module in the socket (shares SPI1 with the SPI NOR)
option(AK_KIT_NRF24 "Hold the nRF24 CSN (PB9) high" OFF)
# Bootloader also runs an APP without image header (raw SWD flash, debug only)
option(AK_BOOT_ALLOW_RAW_APP "Bootloader runs images without header" OFF)
set(BOARD_DEFS PORT_KIT_NRF24_CSN=$<BOOL:${AK_KIT_NRF24}> BOOT_ALLOW_RAW_APP=$<BOOL:${AK_BOOT_ALLOW_RAW_APP}>)
set(MCU_DEFS USE_STDPERIPH_DRIVER STM32L1XX_MD)
set(MCU_WARN -Wall -Wextra -Wno-unused-parameter -Wshadow -Wundef -Wdouble-promotion)

# SPL compiled once, without our warning set (vendor code).
add_library(spl STATIC ${SPL_SRC})
target_include_directories(spl PUBLIC ${PORT_INC})
target_compile_definitions(spl PUBLIC ${MCU_DEFS})
target_compile_options(spl PRIVATE ${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections)

# p256-m (ECDSA verify): vendor code, built like the SPL; only linked when signing
if(AK_IMAGE_SIGN)
	add_library(p256m STATIC ${P256M_SRC})
	target_include_directories(p256m PUBLIC ${P256M_INC})
	target_compile_options(p256m PRIVATE ${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections)
	set(SIGN_LIBS p256m)
else()
	set(SIGN_LIBS "")
endif()

# nanoMODBUS: vendor code, built like the SPL (no warning set), role from AK_MODBUS
add_library(nanomodbus STATIC ${ROOT}/third_party/nanomodbus/nanomodbus.c)
target_include_directories(nanomodbus PUBLIC ${ROOT}/third_party/nanomodbus)
target_compile_definitions(nanomodbus PUBLIC ${MB_DEFS})
target_compile_options(nanomodbus PRIVATE ${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections)

function(ak_firmware name ldscript)
	cmake_parse_arguments(FW "" "" "SOURCES;DEFS;INCS" ${ARGN})
	add_executable(${name} ${FW_SOURCES})
	set_target_properties(${name} PROPERTIES SUFFIX ".elf")
	target_include_directories(${name} PRIVATE ${FW_INCS} ${BASE_INC} ${PORT_INC})
	target_compile_definitions(${name} PRIVATE ${MCU_DEFS} ${VERSION_DEFS} ${STAGING_DEFS} ${SIGN_DEFS} ${BOARD_DEFS} ${FW_DEFS})
	target_compile_options(${name} PRIVATE ${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections ${MCU_WARN} -Werror)
	target_link_options(${name} PRIVATE ${MCU_FLAGS}
		-T${PORT_DIR}/${ldscript} -L${PORT_DIR} -Wl,--defsym=__app_part_size=${APP_PART_SIZE}
		-Wl,--defsym=__boot_size=${BOOT_SIZE}
		-nostartfiles --specs=nano.specs
		-Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map -Wl,--print-memory-usage)
	target_link_libraries(${name} PRIVATE spl ${SIGN_LIBS} c gcc)
	set_target_properties(${name} PROPERTIES LINK_DEPENDS "${PORT_DIR}/${ldscript};${PORT_DIR}/sections.ld")
endfunction()

ak_firmware(boot boot.ld
	SOURCES
		${PORT_DIR}/startup.c ${PORT_DIR}/port_stm32l151.c ${PORT_DIR}/system_stm32l1xx.c
		${STAGING_SRC} ${COMMON_SRC} ${FW_SRC} ${BOOT_SRC}
	DEFS AK_BOOTLOADER
)
add_custom_command(TARGET boot POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:boot> ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
	COMMENT "boot.bin")

ak_firmware(app app.ld
	SOURCES
		${PORT_DIR}/startup.c ${PORT_DIR}/port_stm32l151.c ${PORT_DIR}/system_stm32l1xx.c
		${PORT_DIR}/fw_header.c ${STAGING_SRC}
		${KERNEL_SRC} ${COMMON_SRC} ${FW_SRC} ${SYS_SRC} ${APP_SRC}
		${MB_SRC} ${PORT_DIR}/rs485.c
	INCS ${SRC}/app ${MB_INC}
)
target_link_libraries(app PRIVATE nanomodbus)
# Fill header CRC/size into app.bin -> app.img, and patch .fw_header in app.elf
# so that flashing the .elf over SWD also yields a valid image.
add_custom_command(TARGET app POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:app> ${CMAKE_CURRENT_BINARY_DIR}/app.bin
	COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/mkimage.py patch ${CMAKE_CURRENT_BINARY_DIR}/app.bin
		-o ${CMAKE_CURRENT_BINARY_DIR}/app.img --elf $<TARGET_FILE:app> --objcopy ${CMAKE_OBJCOPY} ${SIGN_ARGS}
	COMMENT "app.img (patched header)")
