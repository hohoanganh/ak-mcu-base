# Shortcuts around CMake.
#   make test     host unit tests + simulator end-to-end test
#   make test-fast  the same with the power cut tests sampled (seconds, not a minute)
#   make sim      build and run the simulator (bootloader + app)
#   make stm32    build boot.elf/.bin, app.elf/.bin, app.img for STM32L151
#                 (OTA staging on external SPI flash, APP 116K)
#   make stm32-gateway    app as Modbus TCP (W5500 on J6) -> RS485 gateway
#   make stm32-sign       same, images must carry an ECDSA P-256 signature (BOOT 16K)
#   make stm32-internal   same, staging in internal flash (APP 58K)
#   make stm32-mbmaster   app as Modbus RTU master (shell: mb read / mb write)
#   make stm32-demo       AK Base Kit demo (PlatformIO env demo)
#   make stm32-kit-tools  demo + SPI sniffer + nRF24 tools (env kit_tools)
#   make stm32-remote     AK Base Kit 2.1 bench remote, below 52K (env remote)
#   make stm32-rc-tx      remote with sticks + nRF24 in J6 (env rc_tx, docs/drone-ak-mcu.md P1)
#   make stm32-rc-rx      its receiver, radio half of the future flight controller (env rc_rx)
#   make stm32-all        every STM32L151 build above, as CI builds them
#   make stm32g0          boot + app for STM32G071RB (NUCLEO-G071RB)
#   make renode           run the STM32G071 build in the Renode emulator (tests/renode)
#   make clean

BUILD_HOST  := build/host
BUILD_STM32 := build/stm32l151

.PHONY: all host test test-fast sim stm32 stm32-gateway stm32-sign stm32-internal stm32-mbmaster stm32-demo \
	stm32-kit-tools stm32-remote stm32-rc-tx stm32-rc-rx stm32-all stm32g0 renode clean

all: test stm32

host:
	cmake -S . -B $(BUILD_HOST)
	cmake --build $(BUILD_HOST) -j

test: host
	cd $(BUILD_HOST) && ctest --output-on-failure -LE fast_only

# a few seconds instead of a minute: power cut tests sampled; make test before a commit
test-fast: host
	cd $(BUILD_HOST) && ctest --output-on-failure -LE full

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

STM32_TC := -DAK_PORT=stm32l151 -DAK_STAGING=external -DCMAKE_TOOLCHAIN_FILE=src/port/stm32l151/arm-none-eabi.cmake

stm32-mbmaster:
	cmake -S . -B $(BUILD_STM32)-mbmaster $(STM32_TC) -DAK_MODBUS=master
	cmake --build $(BUILD_STM32)-mbmaster -j

stm32-demo:
	cmake -S . -B $(BUILD_STM32)-demo $(STM32_TC) -DAK_APP=demo
	cmake --build $(BUILD_STM32)-demo -j

stm32-kit-tools:
	cmake -S . -B $(BUILD_STM32)-kit_tools $(STM32_TC) -DAK_APP=kit_tools
	cmake --build $(BUILD_STM32)-kit_tools -j

stm32-remote:
	cmake -S . -B $(BUILD_STM32)-remote $(STM32_TC) -DAK_APP=remote
	cmake --build $(BUILD_STM32)-remote -j

stm32-rc-tx:
	cmake -S . -B $(BUILD_STM32)-rc_tx $(STM32_TC) -DAK_APP=rc_tx
	cmake --build $(BUILD_STM32)-rc_tx -j

stm32-rc-rx:
	cmake -S . -B $(BUILD_STM32)-rc_rx $(STM32_TC) -DAK_APP=rc_rx
	cmake --build $(BUILD_STM32)-rc_rx -j

stm32-all: stm32 stm32-internal stm32-sign stm32-gateway stm32-mbmaster stm32-demo stm32-kit-tools stm32-remote \
	stm32-rc-tx stm32-rc-rx

clean:
	rm -rf build .pio
