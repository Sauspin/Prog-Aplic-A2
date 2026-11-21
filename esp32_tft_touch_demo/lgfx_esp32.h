#pragma once
// Configuração da LovyanGFX para a tela 2.8" ILI9341 + XPT2046 no ESP32.
// Tudo é configurado aqui no código: não é preciso editar arquivos da biblioteca.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

class LGFX : public lgfx::LGFX_Device
{
#if defined(PANEL_ST7789)
  lgfx::Panel_ST7789  _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Touch_XPT2046 _touch;
  lgfx::Light_PWM     _light;

public:
  LGFX(void)
  {
    { // Barramento SPI
      auto cfg = _bus.config();
      cfg.spi_host    = SPI3_HOST;     // VSPI
      cfg.spi_mode    = 0;
      cfg.freq_write  = SPI_FREQ_WRITE;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_SCLK;
      cfg.pin_mosi    = PIN_MOSI;
      cfg.pin_miso    = PIN_MISO;
      cfg.pin_dc      = TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    { // Painel
      auto cfg = _panel.config();
      cfg.pin_cs           = TFT_CS;
      cfg.pin_rst          = TFT_RST;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = PANEL_INVERT;
      cfg.rgb_order        = PANEL_RGB_ORDER;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;   // touch está no mesmo SPI
      _panel.config(cfg);
    }
#if TFT_BL >= 0
    { // Backlight com PWM (só se o pino LED estiver num GPIO)
      auto cfg = _light.config();
      cfg.pin_bl      = TFT_BL;
      cfg.invert      = false;
      cfg.freq        = BL_PWM_FREQ;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
    { // Touch resistivo XPT2046
      auto cfg = _touch.config();
      cfg.x_min           = 300;     // valores brutos aproximados;
      cfg.x_max           = 3900;    // a calibração na 1ª inicialização
      cfg.y_min           = 200;     // corrige isso com precisão.
      cfg.y_max           = 3800;
      cfg.pin_int         = TOUCH_IRQ;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;
      cfg.spi_host        = SPI3_HOST;
      cfg.freq            = 1000000;
      cfg.pin_sclk        = PIN_SCLK;
      cfg.pin_mosi        = PIN_MOSI;
      cfg.pin_miso        = PIN_MISO;
      cfg.pin_cs          = TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
