# usb-exi-adapter - CMakeLists.txt
#
# RP2040 用ファームウェアのビルド定義。
#   PICO_SDK_PATH を設定してから:
#     cmake -S hardware/usb-exi-adapter -B hardware/usb-exi-adapter/build
#     cmake --build hardware/usb-exi-adapter/build -j
#
# ライセンス: MIT

cmake_minimum_required(VERSION 3.13)

# pico-sdk の取り込み（PICO_SDK_PATH 環境変数を参照）
if (NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH が未設定です。pico-sdk を clone して環境変数を設定してください。")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(usb_exi_adapter C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(usb_exi_adapter
    src/main.c
    src/exi_slave.c
)

# PIO プログラム（exi.pio → exi.pio.h を生成してターゲットに追加）
pico_generate_pio_header(usb_exi_adapter ${CMAKE_CURRENT_LIST_DIR}/src/exi.pio)

target_include_directories(usb_exi_adapter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(usb_exi_adapter
    pico_stdlib
    pico_multicore
    pico_sync
    hardware_pio
    tinyusb_host
    tinyusb_board
)

# USB はホストとして使うため、stdio の USB 出力は無効化する
pico_enable_stdio_usb(usb_exi_adapter 0)
pico_enable_stdio_uart(usb_exi_adapter 1)

pico_add_extra_outputs(usb_exi_adapter)
