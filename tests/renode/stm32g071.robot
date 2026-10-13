*** Settings ***
Documentation       The stm32g071 build on Renode's STM32G0 platform: the bootloader
...                 verifies the app image, starts it through a reset, the app
...                 comes up and answers on its shell; without an app the
...                 bootloader enters the loader. Needs build/stm32g071 (make stm32g0).
Suite Setup         Setup
Suite Teardown      Teardown
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}

*** Variables ***
${UART}             sysbus.usart2
${BUILD}            ${CURDIR}/../../build/stm32g071

*** Keywords ***
Create Machine
    [Arguments]                 ${with_app}
    Execute Command             using sysbus
    Execute Command             mach create "ak-g071"
    Execute Command             machine LoadPlatformDescription @platforms/cpus/stm32g0.repl
    # the flash alias at 0 that every reset fetches SP/PC from (see stm32g071.resc)
    Execute Command             machine LoadPlatformDescriptionFromString "boot_alias: Memory.MappedMemory @ sysbus 0x0 { size: 0x3000 }"
    IF  ${with_app}
        Execute Command         sysbus LoadELF @${BUILD}/app.elf
    END
    Execute Command             sysbus LoadELF @${BUILD}/boot.elf
    Execute Command             sysbus LoadBinary @${BUILD}/boot.bin 0x0
    Execute Command             cpu VectorTableOffset 0x08000000
    Create Terminal Tester      ${UART}

*** Test Cases ***
Bootloader Verifies And Runs The App
    Create Machine              True
    Start Emulation
    Wait For Line On Uart       ak-mcu-base bootloader v
    # two or more spaces would split the argument: spell them out
    Wait For Line On Uart       app${SPACE * 4}: v1.
    Wait For Line On Uart       staging: not checked
    Wait For Line On Uart       cmd 0, attempts 0, trial 0 -> run
    Wait For Line On Uart       jump to 0x08003100
    Wait For Line On Uart       build 0, board ak-g071
    Wait For Line On Uart       watchdog 8000 ms, heartbeat 1000 ms

App Shell Answers
    Create Machine              True
    Start Emulation
    Wait For Line On Uart       ak-mcu-base app v1.
    Wait For Prompt On Uart     >
    Write Line To Uart          ver
    Wait For Line On Uart       build 0, kernel
    Write Line To Uart          info
    Wait For Line On Uart       app${SPACE * 6}0x08003000
    Wait For Line On Uart       staging${SPACE * 2}0x08011000
    Wait For Line On Uart       boot_ctrl: cmd 0, attempts 0
    Write Line To Uart          stat
    Wait For Line On Uart       crash log: 0 record(s)

Bootloader Refuses A Missing App And Enters The Loader
    Create Machine              False
    Start Emulation
    Wait For Line On Uart       app${SPACE * 4}: no image
    Wait For Line On Uart       -> loader
    Wait For Line On Uart       loader mode - send firmware with tools/ak_fw.py
