# STM32G0 CMSIS device headers

From <https://github.com/STMicroelectronics/cmsis_device_g0> (Apache-2.0, see LICENSE.md), only the three
headers the `stm32g071` port needs: `stm32g0xx.h`, `stm32g071xx.h`, `system_stm32g0xx.h`. The Cortex-M0+
core header (`core_cm0plus.h`) comes from `third_party/stm32l1/CMSIS/Include`. No HAL, no LL: the port
talks to the registers directly (`src/port/stm32g071/`).
