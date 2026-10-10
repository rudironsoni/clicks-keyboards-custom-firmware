cmake_minimum_required(VERSION 3.20)
project(ck5200_qmk C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

set(QMK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/qmk_firmware" CACHE PATH "QMK source")
set(TINYUSB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/tinyusb" CACHE PATH "TinyUSB source")
set(WCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ch32v20x" CACHE PATH "OpenWCH CH32V20x SDK")
set(WCH_SRC "${WCH_DIR}/EVT/EXAM/SRC")

foreach(req IN ITEMS
    "${QMK_DIR}/quantum/main.c"
    "${TINYUSB_DIR}/src/tusb.c"
    "${WCH_SRC}/Startup/startup_ch32v20x_D6.S")
    if(NOT EXISTS "${req}")
        message(FATAL_ERROR "Missing dependency: ${req}. Run scripts/bootstrap.sh first.")
    endif()
endforeach()

include(cmake/qmk_core.cmake)

set(QMK_INCLUDE_DIRS
    "${QMK_DIR}"
    "${QMK_DIR}/quantum"
    "${QMK_DIR}/quantum/bootmagic"
    "${QMK_DIR}/quantum/debounce"
    "${QMK_DIR}/quantum/logging"
    "${QMK_DIR}/quantum/keymap_extras"
    "${QMK_DIR}/quantum/process_keycode"
    "${QMK_DIR}/quantum/send_string"
    "${QMK_DIR}/quantum/sequencer"
    "${QMK_DIR}/quantum/wear_leveling"
    "${QMK_DIR}/tmk_core"
    "${QMK_DIR}/tmk_core/common"
    "${QMK_DIR}/tmk_core/protocol"
    "${QMK_DIR}/platforms"
    "${QMK_DIR}/drivers"
    "${QMK_DIR}/drivers/eeprom"
    "${QMK_DIR}/lib"
    "${QMK_DIR}/lib/fnv"
)

set(TINYUSB_SOURCES
    "${TINYUSB_DIR}/src/tusb.c"
    "${TINYUSB_DIR}/src/common/tusb_fifo.c"
    "${TINYUSB_DIR}/src/device/usbd.c"
    "${TINYUSB_DIR}/src/class/hid/hid_device.c"
    "${TINYUSB_DIR}/src/class/vendor/vendor_device.c"
    "${TINYUSB_DIR}/src/portable/wch/dcd_ch32_usbfs.c"
)

set(WCH_SOURCES
    "${WCH_SRC}/Core/core_riscv.c"
    "${WCH_SRC}/Peripheral/src/ch32v20x_flash.c"
    "${WCH_SRC}/Peripheral/src/ch32v20x_gpio.c"
    "${WCH_SRC}/Peripheral/src/ch32v20x_misc.c"
    "${WCH_SRC}/Peripheral/src/ch32v20x_rcc.c"
    "${TINYUSB_DIR}/hw/bsp/ch32v20x/system_ch32v20x.c"
    "${WCH_SRC}/Startup/startup_ch32v20x_D6.S"
)

set(CK5200_SOURCES
    firmware/keyboard/ck5200.c
    firmware/keyboard/matrix.c
    firmware/platform/ch32v20x/gpio.c
    firmware/platform/ch32v20x/platform.c
    firmware/platform/ch32v20x/protocol.c
    firmware/platform/ch32v20x/usb_irq.c
    firmware/platform/ch32v20x/ck5200_update_protocol.c
    firmware/platform/ch32v20x/ck5200_staging.c
    firmware/platform/ch32v20x/ck5200_keymap.c
    firmware/platform/ch32v20x/ck5200_backlight.c
    firmware/platform/ch32v20x/iap2.c
    firmware/platform/ch32v20x/iap2_auth.c
    firmware/platform/ch32v20x/wear_leveling_ch32v20x.c
    firmware/usb/usb_descriptors.c
    "${QMK_DIR}/quantum/dynamic_keymap.c"
    "${QMK_DIR}/quantum/wear_leveling/wear_leveling.c"
    "${QMK_DIR}/drivers/eeprom/eeprom_wear_leveling.c"
)

add_executable(ck5200_qmk.elf ${QMK_CORE_SOURCES} ${TINYUSB_SOURCES} ${WCH_SOURCES} ${CK5200_SOURCES})

# The session stack favors size over speed: it runs at most once per ms and
# must share the 27 KB application region with the rest of the firmware.
set_source_files_properties(
    firmware/platform/ch32v20x/iap2.c
    firmware/platform/ch32v20x/iap2_auth.c
    PROPERTIES COMPILE_OPTIONS "-Oz"
)

set_property(TARGET ck5200_qmk.elf APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/platform/ch32v20x/linker.ld")

target_include_directories(ck5200_qmk.elf PRIVATE
    firmware/keyboard
    firmware/keyboard/keymaps/diagnostic
    firmware/platform/ch32v20x
    firmware/usb
    ${QMK_INCLUDE_DIRS}
    "${TINYUSB_DIR}/src"
    "${TINYUSB_DIR}/hw"
    "${WCH_SRC}/Core"
    "${WCH_SRC}/Peripheral/inc"
    "${TINYUSB_DIR}/hw/bsp/ch32v20x"
)

target_compile_definitions(ck5200_qmk.elf PRIVATE
    CH32V20x_D6
    SYSCLK_FREQ_144MHz_HSI=144000000
    CFG_TUD_WCH_USBIP_USBFS=1
    BOARD_TUD_RHPORT=1
    QMK_KEYBOARD_H="ck5200.h"
    KEYMAP_C="keymap.c"
    NO_PRINT
    NO_DEBUG
    FAST_TIMER_T_SIZE=32
    EEPROM_ENABLE
    EEPROM_DRIVER
)

target_compile_options(ck5200_qmk.elf PRIVATE
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -mcmodel=medany
    -msmall-data-limit=8
    -mno-save-restore
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fno-strict-aliasing
    -Wall
    -Wextra
    -Wno-unused-parameter
    -include
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/keyboard/config.h
    -Os
    -Oz
    -flto=auto
)

target_link_options(ck5200_qmk.elf PRIVATE
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ck5200_qmk.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/firmware/platform/ch32v20x/linker.ld
)

add_custom_command(TARGET ck5200_qmk.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ck5200_qmk.elf> ${CMAKE_CURRENT_BINARY_DIR}/ck5200_qmk.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:ck5200_qmk.elf> ${CMAKE_CURRENT_BINARY_DIR}/ck5200_qmk.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:ck5200_qmk.elf>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/tools
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_image.py
            ${CMAKE_CURRENT_BINARY_DIR}/ck5200_qmk.bin --elf $<TARGET_FILE:ck5200_qmk.elf>
    VERBATIM
)
