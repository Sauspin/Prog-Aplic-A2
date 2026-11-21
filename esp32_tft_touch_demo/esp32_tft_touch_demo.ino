// =====================================================================
//  ESP32 + TFT 2.8" ILI9341 + touch XPT2046  -  Demo de interface moderna
// =====================================================================
//  Biblioteca necessária (Arduino IDE > Gerenciar bibliotecas):
//      LovyanGFX  (by lovyan03)
//  Placa: "ESP32 Dev Module"
//
//  Pinos: veja config.h
//  IMPORTANTE: nesta montagem CS e DC usam os pinos TX/RX (GPIO 1 e 3),
//  então o programa não usa Serial (o Monitor Serial fica em silêncio).
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
uint8_t plat_load_setting(const char* key, uint8_t def) { return prefs.getUChar(key, def); }
void plat_save_setting(const char* key, uint8_t value)
{
  if (prefs.getUChar(key, value + 1) != value) prefs.putUChar(key, value);   // evita gravar à toa
}
uint32_t plat_free_heap() { return ESP.getFreeHeap(); }
uint32_t plat_min_free_heap() { return ESP.getMinFreeHeap(); }
uint32_t plat_cpu_mhz() { return ESP.getCpuFreqMHz(); }
const char* plat_chip_name() { return ESP.getChipModel(); }

void plat_calibrate_touch()
{
  uint16_t cal[8];
  lcd.setBrightness(200);
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
  // Sem Serial.begin(): TX/RX estão ligados nos pinos CS/DC da tela.
  lcd.init();
  lcd.setBrightness(0);          // a interface acende o LED com um fade
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
    lcd.setBrightness(200);
    lcd.setTextColor(TFT_RED);
    lcd.drawString("Sem memoria para o buffer!", 10, 10);
    while (true) delay(1000);
  }
}

void loop()
{
  ui_loop();
}
