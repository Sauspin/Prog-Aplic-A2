# Simulador para PC (Linux / macOS / Windows) usando SDL2.
# Roda exatamente a mesma interface (ui.cpp) do ESP32; o mouse faz o papel do dedo.
#
#   git clone https://github.com/lovyan03/LovyanGFX.git   (ou informe -DLOVYANGFX_DIR=...)
#   cmake -B build -DLOVYANGFX_DIR=/caminho/LovyanGFX && cmake --build build -j
#   ./build/tft_sim
cmake_minimum_required(VERSION 3.10)
project(tft_sim CXX C)
set(CMAKE_CXX_STANDARD 17)

set(LOVYANGFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/LovyanGFX" CACHE PATH "Pasta da biblioteca LovyanGFX")
set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../esp32_tft_touch_demo")

add_definitions(-DLGFX_SDL)
file(GLOB LGFX_SRC
  ${LOVYANGFX_DIR}/src/lgfx/Fonts/efont/*.c
  ${LOVYANGFX_DIR}/src/lgfx/Fonts/IPA/*.c
  ${LOVYANGFX_DIR}/src/lgfx/utility/*.c
  ${LOVYANGFX_DIR}/src/lgfx/v1/*.cpp
  ${LOVYANGFX_DIR}/src/lgfx/v1/panel/*.cpp
  ${LOVYANGFX_DIR}/src/lgfx/v1/touch/*.cpp
  ${LOVYANGFX_DIR}/src/lgfx/v1/lv_font/*.c)

add_executable(tft_sim sim_main.cpp ${APP_DIR}/ui.cpp ${APP_DIR}/fonts_inter.cpp ${LGFX_SRC})
target_include_directories(tft_sim PUBLIC ${LOVYANGFX_DIR}/src ${APP_DIR})

find_package(SDL2 REQUIRED)
target_include_directories(tft_sim PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(tft_sim ${SDL2_LIBRARIES} pthread)
