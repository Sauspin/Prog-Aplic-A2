#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Inicializa a interface. "out" é onde os quadros são desenhados
// (a tela de verdade no ESP32, ou a janela no simulador).
bool ui_begin(lgfx::LovyanGFX* out);

// Chame sempre no loop(): lê o touch, anima e desenha um quadro.
void ui_loop();
