// =====================================================================
//  ESP32 + TFT 2.8" ILI9341 + touch XPT2046  -  Demo de interface moderna
// =====================================================================
//  Biblioteca necessária (Arduino IDE > Gerenciar bibliotecas):
//      LovyanGFX  (by lovyan03)
//  Placa: "ESP32 Dev Module"
//
//  Pinos: veja config.h
//
//  Na 1ª vez que ligar, a tela pede para tocar nos 4 cantos (calibração).
//  O resultado fica salvo na flash. Para recalibrar: mantenha o dedo na
//  tela enquanto liga/reseta, ou use Ajustes > "Calibrar touch".
// =====================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include "lgfx_esp32.h"
#include "platform.h"
#include "ui.h"

static LGFX lcd;
static Preferences prefs;

// ---------------- implementação da "plataforma" para o ESP32 ----------------
uint32_t plat_millis() { return millis(); }

bool plat_get_touch(int16_t* x, int16_t* y)
{
  lgfx::touch_point_t tp;
  if (lcd.getTouch(&tp)) { *x = tp.x; *y = tp.y; return true; }
  return false;
}

bool plat_has_backlight() { return TFT_BL >= 0; }
void plat_set_brightness(uint8_t level) { if (TFT_BL >= 0) lcd.setBrightness(level); }
uint32_t plat_free_heap() { return ESP.getFreeHeap(); }
uint32_t plat_min_free_heap() { return ESP.getMinFreeHeap(); }
uint32_t plat_cpu_mhz() { return ESP.getCpuFreqMHz(); }
const char* plat_chip_name() { return ESP.getChipModel(); }

void plat_calibrate_touch()
{
  uint16_t cal[8];
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Calibracao do touch", lcd.width() / 2, lcd.height() / 2 - 12);
  lcd.setTextColor(TFT_LIGHTGREY);
  lcd.drawString("Toque no centro de cada seta", lcd.width() / 2, lcd.height() / 2 + 10);
  lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 18);
  prefs.putBytes("cal", cal, sizeof(cal));
  lcd.fillScreen(TFT_BLACK);
}

// ----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);
  lcd.fillScreen(TFT_BLACK);

  prefs.begin("tftdemo", false);
  uint16_t cal[8];
  bool hasCal = prefs.getBytesLength("cal") == sizeof(cal) && prefs.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);
  int16_t tx, ty;
  bool holding = plat_get_touch(&tx, &ty);   // dedo na tela ao ligar = recalibrar
  if (hasCal && !holding) lcd.setTouchCalibrate(cal);
  else plat_calibrate_touch();

  if (!ui_begin(&lcd)) {
    lcd.setTextColor(TFT_RED);
    lcd.drawString("Sem memoria para o buffer!", 10, 10);
    Serial.println("ui_begin falhou: memoria insuficiente");
    while (true) delay(1000);
  }
  Serial.printf("UI pronta. RAM livre: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop()
{
  ui_loop();
}
