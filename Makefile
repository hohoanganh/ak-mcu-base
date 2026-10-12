# Shortcuts around CMake.
#   make test     host unit tests + simulator end-to-end test
#   make sim      build and run the simulator (bootloader + app)
#   make stm32    build boot.elf/.bin, app.elf/.bin, app.img for STM32L151
#                 (OTA staging on external SPI flash, APP 116K)
#   make stm32-gateway    app as Modbus TCP (W5500 on J6) -> RS485 gateway
#   make stm32-sign       same, images must carry an ECDSA P-256 signature (BOOT 16K)
#   make stm32-internal   same, staging in internal flash (APP 58K)
#   make stm32g0          boot + app for STM32G071RB (NUCLEO-G071RB)
#   make renode           run the STM32G071 build in the Renode emulator (tests/renode)
#   make clean

BUILD_HOST  := build/host
BUILD_STM32 := build/stm32l151

.PHONY: all host test sim stm32 stm32-gateway stm32-sign stm32-internal stm32g0 renode clean

all: test stm32

host:
	cmake -S . -B $(BUILD_HOST)
	cmake --build $(BUILD_HOST) -j

test: host
	cd $(BUILD_HOST) && ctest --output-on-failure

sim: host
	$(BUILD_HOST)/ak_sim --flash $(BUILD_HOST)/ak_sim_flash.bin

stm32:
	cmake -S . -B $(BUILD_STM32) -DAK_PORT=stm32l151 -DAK_STAGING=external -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
	cmake --build $(BUILD_STM32) -j

stm32-gateway:
	cmake -S . -B $(BUILD_STM32)-gateway -DAK_PORT=stm32l151 -DAK_STAGING=external -DAK_MODBUS=gateway -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
	cmake --build $(BUILD_STM32)-gateway -j

stm32-sign:
	cmake -S . -B $(BUILD_STM32)-sign -DAK_PORT=stm32l151 -DAK_STAGING=external -DAK_IMAGE_SIGN=ON -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
	cmake --build $(BUILD_STM32)-sign -j

stm32g0:
	cmake -S . -B build/stm32g071 -DAK_PORT=stm32g071 -DAK_MODBUS=none -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
	cmake --build build/stm32g071 -j

# Renode (https://renode.io): RENODE_TEST = path to renode-test of a portable
# release; results land in build/renode
RENODE_TEST ?= renode-test
renode: stm32g0
	$(RENODE_TEST) --results-dir build/renode tests/renode/stm32g071.robot

stm32-internal:
	cmake -S . -B $(BUILD_STM32)-internal -DAK_PORT=stm32l151 -DAK_STAGING=internal -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake
	cmake --build $(BUILD_STM32)-internal -j

clean:
	rm -rf build .pio
