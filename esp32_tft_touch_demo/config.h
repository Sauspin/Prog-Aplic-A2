#pragma once
// =====================================================================
//  CONFIGURAÇÃO DE HARDWARE  -  confira com a SUA fiação!
// =====================================================================
//  Tela : 2.8" TFT SPI 240x320 (placa vermelha "V1.1")
//         controlador ILI9341 + touch resistivo XPT2046 + slot SD
//  MCU  : ESP32 DevKit 38 pinos (ESP-WROOM-32)
//
//  Ligação usada por padrão (a mais comum para ESP32 + ILI9341).
//  A tela e o touch compartilham o MESMO barramento SPI (VSPI):
//
//   Pino da tela      ->  ESP32
//   -------------------------------
//   VCC               ->  3V3   (ou 5V: a placa tem regulador)
//   GND               ->  GND
//   CS                ->  GPIO 15
//   RESET             ->  GPIO 4
//   DC                ->  GPIO 2
//   SDI(MOSI)         ->  GPIO 23
//   SCK               ->  GPIO 18
//   LED               ->  3V3   (ou um GPIO, veja TFT_BL)
//   SDO(MISO)         ->  GPIO 19
//   T_CLK             ->  GPIO 18  (mesmo fio do SCK)
//   T_CS              ->  GPIO 21
//   T_DIN             ->  GPIO 23  (mesmo fio do MOSI)
//   T_DO              ->  GPIO 19  (mesmo fio do MISO)
//   T_IRQ             ->  não ligado (opcional)
//
//  Se a sua fiação for diferente, é só trocar os números abaixo.
// =====================================================================

// ---- Barramento SPI (compartilhado tela + touch) ----
#define PIN_SCLK      18
#define PIN_MOSI      23
#define PIN_MISO      19

// ---- Tela ----
#define TFT_CS        15
#define TFT_DC        2
#define TFT_RST       4      // -1 se o RESET estiver ligado no EN/3V3
#define TFT_BL        -1     // GPIO do pino LED p/ controlar brilho (PWM). -1 = LED ligado direto no 3V3

// ---- Touch XPT2046 ----
#define TOUCH_CS      21
#define TOUCH_IRQ     -1     // GPIO do T_IRQ, ou -1 se não estiver ligado

// ---- Controlador ----
// A grande maioria dessas placas vermelhas usa ILI9341. Se a imagem
// aparecer com cores invertidas/espelhada ou nada aparecer, tente ST7789.
#define PANEL_ILI9341       // ou: #define PANEL_ST7789

// Se as cores saírem invertidas (fundo branco em vez de preto), troque:
#define PANEL_INVERT        false
// Se vermelho e azul estiverem trocados, troque:
#define PANEL_RGB_ORDER     false

// Rotação: 1 = paisagem (conector à esquerda). Use 3 se ficar de cabeça para baixo.
#define SCREEN_ROTATION     1

// Frequência do SPI da tela. Se aparecer "lixo" na imagem (fios longos),
// baixe para 27000000 ou 20000000.
#define SPI_FREQ_WRITE      40000000
