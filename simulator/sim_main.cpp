// Simulador de PC: mesma interface do ESP32 numa janela SDL de 320x240.
// O mouse simula o dedo no touch.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "platform.h"
#include "ui.h"

static LGFX lcd(320, 240, 2);   // janela com zoom 2x

uint32_t plat_millis() { return lgfx::millis(); }
bool plat_get_touch(int16_t* x, int16_t* y)
{
  lgfx::touch_point_t tp;
  if (lcd.getTouch(&tp)) { *x = tp.x; *y = tp.y; return true; }
  return false;
}
void plat_calibrate_touch() { std::puts("(simulador) calibração não é necessária"); }
bool plat_has_backlight() { return false; }
void plat_set_brightness(uint8_t) {}
uint32_t plat_free_heap() { return 180 * 1024; }
uint32_t plat_min_free_heap() { return 150 * 1024; }
uint32_t plat_cpu_mhz() { return 240; }
const char* plat_chip_name() { return "Simulador"; }

void setup()
{
  lcd.init();
  if (!ui_begin(&lcd)) std::puts("ui_begin falhou");
}

void loop()
{
  ui_loop();
  lgfx::delay(5);
}

#if defined(SDL_h_)
static int user_func(bool* running)
{
  setup();
  do { loop(); } while (*running);
  return 0;
}
int main(int, char**) { return lgfx::Panel_sdl::main(user_func); }
#endif
