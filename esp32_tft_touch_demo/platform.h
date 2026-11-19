#pragma once
// Pequena camada de "plataforma": a interface (ui.cpp) não sabe se está
// rodando no ESP32 ou no simulador do PC. Cada lado implementa estas funções.
#include <stdint.h>

uint32_t    plat_millis();
bool        plat_get_touch(int16_t* x, int16_t* y);   // true = dedo na tela
void        plat_calibrate_touch();                   // roda a calibração e salva
bool        plat_has_backlight();
void        plat_set_brightness(uint8_t level);       // 0..255
uint32_t    plat_free_heap();
uint32_t    plat_min_free_heap();
uint32_t    plat_cpu_mhz();
const char* plat_chip_name();
