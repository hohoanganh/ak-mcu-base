# Host port: unit tests + ak_sim simulator
set(HOST_DIR ${SRC}/port/host)
set(HOST_WARN -Wall -Wextra -Wno-unused-parameter -Werror)

enable_testing()

# --- kernel tests (own task table in tests/kernel) ---
add_executable(test_kernel
	${ROOT}/tests/test_kernel.c
	${KERNEL_SRC} ${COMMON_SRC}
	${HOST_DIR}/port_host.c
)
target_include_directories(test_kernel PRIVATE ${ROOT}/tests/kernel ${ROOT}/tests ${BASE_INC} ${HOST_DIR})
target_compile_options(test_kernel PRIVATE ${HOST_WARN} -fsanitize=address,undefined -g)
target_link_options(test_kernel PRIVATE -fsanitize=address,undefined)
add_test(NAME kernel COMMAND test_kernel)

# the same tests on a kernel without dynamic messages (no malloc/free linked)
add_executable(test_kernel_nodyn
	${ROOT}/tests/test_kernel.c
	${KERNEL_SRC} ${COMMON_SRC}
	${HOST_DIR}/port_host.c
)
target_include_directories(test_kernel_nodyn PRIVATE ${ROOT}/tests/kernel ${ROOT}/tests ${BASE_INC} ${HOST_DIR})
target_compile_definitions(test_kernel_nodyn PRIVATE AK_DYNAMIC_MSG_POOL_SIZE=0)
target_compile_options(test_kernel_nodyn PRIVATE ${HOST_WARN} -fsanitize=address,undefined -g)
target_link_options(test_kernel_nodyn PRIVATE -fsanitize=address,undefined)
add_test(NAME kernel_nodyn COMMAND test_kernel_nodyn)

# --- firmware update + bootloader tests ---
# p256-m (vendor, no warning set): test_fw checks signed images
add_library(p256m_host STATIC ${P256M_SRC})
target_include_directories(p256m_host PUBLIC ${P256M_INC})
target_compile_options(p256m_host PRIVATE -w -g -fsanitize=address,undefined)

# test_fw signs with a key it makes itself: it gets a writable public key, not fw_pubkey.c
set(TEST_FW_SRC ${FW_SRC})
list(REMOVE_ITEM TEST_FW_SRC ${SRC}/services/fw/fw_pubkey.c)
add_executable(test_fw
	${ROOT}/tests/test_fw.c
	${ROOT}/tests/test_pubkey.c
	${COMMON_SRC} ${TEST_FW_SRC} ${SYS_SRC} ${BOOT_SRC}
	${HOST_DIR}/port_host.c
)
target_include_directories(test_fw PRIVATE ${ROOT}/tests ${BASE_INC} ${HOST_DIR})
target_compile_definitions(test_fw PRIVATE AK_SIM ${VERSION_DEFS} FW_IMAGE_SIGN=1)
target_link_libraries(test_fw PRIVATE p256m_host)
target_compile_options(test_fw PRIVATE ${HOST_WARN} -fsanitize=address,undefined -g)
target_link_options(test_fw PRIVATE -fsanitize=address,undefined)
add_test(NAME fw COMMAND test_fw)
# the same with the power cut tests sampled (every 7th flash operation): only
# for make test-fast. Label "full" = left out of test-fast, "fast_only" = left
# out of make test and CI
add_test(NAME fw_fast COMMAND test_fw)
set_tests_properties(fw PROPERTIES LABELS full)
set_tests_properties(fw_fast PROPERTIES LABELS fast_only ENVIRONMENT AK_TEST_FAST=1)

# --- Modbus services (vendor library built without our warning set) ---
add_library(nanomodbus_host STATIC ${ROOT}/third_party/nanomodbus/nanomodbus.c)
target_include_directories(nanomodbus_host PUBLIC ${ROOT}/third_party/nanomodbus)
target_compile_options(nanomodbus_host PRIVATE -w -g)

add_executable(test_modbus
	${ROOT}/tests/test_modbus.c
	${COMMON_SRC} ${FW_SRC} ${MB_SRC}
	${HOST_DIR}/port_host.c
)
target_include_directories(test_modbus PRIVATE ${ROOT}/tests ${BASE_INC} ${MB_INC} ${HOST_DIR})
target_compile_definitions(test_modbus PRIVATE AK_SIM ${VERSION_DEFS})
target_compile_options(test_modbus PRIVATE ${HOST_WARN} -fsanitize=address,undefined -g)
target_link_options(test_modbus PRIVATE -fsanitize=address,undefined)
target_link_libraries(test_modbus PRIVATE nanomodbus_host)
add_test(NAME modbus COMMAND test_modbus)

# --- kit demo: screens and game logic, with the kit emulated in the test ---
add_executable(test_demo
	${ROOT}/tests/test_demo.c
	${SRC}/demo/gfx.c ${SRC}/demo/ui_common.c
	${SRC}/demo/scr_clock.c ${SRC}/demo/scr_snake.c ${SRC}/demo/scr_flappy.c ${SRC}/demo/scr_system.c
	${SRC}/demo/scr_dino.c ${SRC}/demo/scr_cube.c ${SRC}/demo/scr_music.c ${SRC}/demo/music.c
	${SRC}/demo/scr_maze.c ${SRC}/demo/scr_video.c ${SRC}/demo/video.c ${SRC}/demo/scr_weather.c
	${SRC}/demo/scr_tetris.c ${SRC}/demo/scr_breakout.c ${SRC}/demo/scr_invaders.c ${SRC}/demo/scr_saver.c ${SRC}/demo/scr_blocks.c ${SRC}/demo/blk_vm.c ${SRC}/demo/scr_pong.c ${SRC}/demo/scr_plot.c
	${KERNEL_SRC} ${COMMON_SRC} ${SYS_SRC}
	${HOST_DIR}/port_host.c
)
target_include_directories(test_demo PRIVATE ${ROOT}/tests/kernel ${ROOT}/tests ${SRC}/demo ${BASE_INC} ${HOST_DIR})
target_compile_options(test_demo PRIVATE ${HOST_WARN} -fsanitize=address,undefined -g)
target_link_options(test_demo PRIVATE -fsanitize=address,undefined)
add_test(NAME demo COMMAND test_demo)

# --- ak_sim: bootloader + app running on the PC ---
add_executable(ak_sim
	${HOST_DIR}/sim_main.c
	${HOST_DIR}/port_host.c
	${KERNEL_SRC} ${COMMON_SRC} ${FW_SRC} ${SYS_SRC} ${MB_SRC} ${BOOT_SRC} ${APP_SRC}
)
target_include_directories(ak_sim PRIVATE ${SRC}/app ${BASE_INC} ${MB_INC} ${HOST_DIR})
target_compile_definitions(ak_sim PRIVATE AK_SIM ${VERSION_DEFS} APP_MODBUS_SLAVE)
target_link_libraries(ak_sim PRIVATE nanomodbus_host)
target_compile_options(ak_sim PRIVATE ${HOST_WARN} -g)

# --- end-to-end: tools/ak_fw.py flashes ak_sim over pipes ---
find_package(Python3 COMPONENTS Interpreter)
# --- block compiler (JavaScript): its own checks, and tests/blk_fixtures.h up to date ---
find_program(NODE_EXECUTABLE NAMES node nodejs)
if(NODE_EXECUTABLE)
	add_test(NAME blocks_js COMMAND ${NODE_EXECUTABLE} ${ROOT}/tests/test_blocks.js)
endif()

if(Python3_FOUND)
	# frame reader of tools/ak_fw.py against log text around the frames
	add_test(NAME ak_fw
		COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_ak_fw.py --tools ${ROOT}/tools)
	add_test(NAME sim_ota
		COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_sim_ota.py
			--sim $<TARGET_FILE:ak_sim> --tools ${ROOT}/tools
			--workdir ${CMAKE_CURRENT_BINARY_DIR})
endif()
