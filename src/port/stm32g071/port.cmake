# Port STM32G071RB (NUCLEO-G071RB): builds boot.elf/.bin, app.elf/.bin and
# app.img. Register-level code on the CMSIS device header in
# third_party/stm32g0 (no HAL / LL); the Cortex-M0+ core header comes from
# third_party/stm32l1/CMSIS/Include. No RS485 port on the Nucleo: AK_MODBUS
# must be "none".
set(PORT_DIR ${SRC}/port/stm32g071)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(NOT AK_MODBUS STREQUAL "none")
	message(FATAL_ERROR "stm32g071: no RS485 port on this board yet, build with -DAK_MODBUS=none")
endif()
if(AK_IMAGE_SIGN)
	message(FATAL_ERROR "stm32g071: no signed-image variant yet")
endif()

# flash map of port_cfg.h, handed to the linker scripts
set(BOOT_SIZE 0x3000)
set(APP_PART_SIZE 0xE000)
message(STATUS "STM32G071: BOOT ${BOOT_SIZE}, APP partition ${APP_PART_SIZE}, staging internal")

set(PORT_INC
	${PORT_DIR}
	${ROOT}/third_party/stm32g0/Include
	${ROOT}/third_party/stm32l1/CMSIS/Include
)

set(MCU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)

option(AK_LTO "Build boot/app with -flto" ON)
if(AK_LTO)
	list(APPEND MCU_FLAGS -flto)
endif()

set(MCU_DEFS STM32G071xx)
set(MCU_WARN -Wall -Wextra -Wno-unused-parameter -Wshadow -Wundef -Wdouble-promotion)

function(ak_firmware name ldscript)
	cmake_parse_arguments(FW "" "" "SOURCES;DEFS;INCS" ${ARGN})
	add_executable(${name} ${FW_SOURCES})
	set_target_properties(${name} PROPERTIES SUFFIX ".elf")
	target_include_directories(${name} PRIVATE ${FW_INCS} ${BASE_INC} ${PORT_INC})
	target_compile_definitions(${name} PRIVATE ${MCU_DEFS} ${VERSION_DEFS} ${FW_DEFS})
	target_compile_options(${name} PRIVATE ${MCU_FLAGS} -Os -g -ffunction-sections -fdata-sections ${MCU_WARN} -Werror)
	target_link_options(${name} PRIVATE ${MCU_FLAGS}
		-T${PORT_DIR}/${ldscript} -L${PORT_DIR}
		-Wl,--defsym=__app_part_size=${APP_PART_SIZE} -Wl,--defsym=__boot_size=${BOOT_SIZE}
		-nostartfiles --specs=nano.specs
		-Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map -Wl,--print-memory-usage)
	target_link_libraries(${name} PRIVATE c gcc)
	set_target_properties(${name} PROPERTIES LINK_DEPENDS "${PORT_DIR}/${ldscript};${PORT_DIR}/sections.ld")
endfunction()

ak_firmware(boot boot.ld
	SOURCES
		${PORT_DIR}/startup.c ${PORT_DIR}/port_stm32g071.c
		${COMMON_SRC} ${FW_SRC} ${BOOT_SRC}
	DEFS AK_BOOTLOADER
)
add_custom_command(TARGET boot POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:boot> ${CMAKE_CURRENT_BINARY_DIR}/boot.bin
	COMMENT "boot.bin")

ak_firmware(app app.ld
	SOURCES
		${PORT_DIR}/startup.c ${PORT_DIR}/port_stm32g071.c ${PORT_DIR}/fw_header.c
		${KERNEL_SRC} ${COMMON_SRC} ${FW_SRC} ${SYS_SRC} ${APP_SRC}
	INCS ${SRC}/app
)
add_custom_command(TARGET app POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:app> ${CMAKE_CURRENT_BINARY_DIR}/app.bin
	COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/mkimage.py patch ${CMAKE_CURRENT_BINARY_DIR}/app.bin
		-o ${CMAKE_CURRENT_BINARY_DIR}/app.img --elf $<TARGET_FILE:app> --objcopy ${CMAKE_OBJCOPY}
	COMMENT "app.img (patched header)")
