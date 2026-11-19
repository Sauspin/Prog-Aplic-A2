// =====================================================================
//  Interface gráfica de demonstração - tema escuro, touch, animações
// =====================================================================
//  Telas:  Início (6 blocos)  ->  Dashboard | Gráficos | Relógio |
//          Medidores | Ajustes | Sistema
//
//  Como a tela é desenhada sem piscar:
//  Um quadro inteiro (320x240x16 bits = 150 KB) não cabe num único bloco
//  de RAM do ESP32. Então desenhamos o quadro em 2 FAIXAS de 120 linhas
//  usando um único buffer de 75 KB: a cena é desenhada duas vezes, cada
//  vez "recortada" (clip) para uma faixa, e a faixa pronta é enviada à
//  tela. Como nada é desenhado direto no LCD, não há tremulação.
// =====================================================================
#include "ui.h"
#include "platform.h"
#include "fonts_inter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using lgfx::textdatum_t;

namespace {

// ---------------------------------------------------------------------
//  Constantes e paleta
// ---------------------------------------------------------------------
constexpr int W = 320;
constexpr int H = 240;
constexpr int BAND_H = 120;
constexpr int BAR_H = 28;          // altura da barra de status

constexpr float PI_F = 3.14159265f;

// Cores em RGB888 (uint32_t). Atenção: na LovyanGFX um literal "int"
// é interpretado como RGB565, por isso sempre usamos estas constantes.
constexpr uint32_t COL_BG    = 0x0B0E14;
constexpr uint32_t COL_CARD  = 0x151A23;
constexpr uint32_t COL_CARD2 = 0x1D2430;
constexpr uint32_t COL_LINE  = 0x2A3342;
constexpr uint32_t COL_TEXT  = 0xE8EDF4;
constexpr uint32_t COL_MUTED = 0x8A94A6;
constexpr uint32_t COL_DIM   = 0x4A5466;
constexpr uint32_t COL_WHITE = 0xFFFFFF;
constexpr uint32_t COL_GOOD  = 0x34D399;
constexpr uint32_t COL_WARN  = 0xFBBF24;
constexpr uint32_t COL_BAD   = 0xF87171;

constexpr uint32_t ACCENTS[] = { 0x22D3EE, 0xA78BFA, 0x34D399, 0xFBBF24, 0xF472B6, 0x60A5FA };
constexpr int N_ACCENTS = sizeof(ACCENTS) / sizeof(ACCENTS[0]);

// ---------------------------------------------------------------------
//  Estado global
// ---------------------------------------------------------------------
lgfx::LovyanGFX* g_out = nullptr;
LGFX_Sprite g_canvas;               // "tela virtual" 320x240 (só uma faixa existe na RAM)
LGFX_Sprite g_view;                 // a faixa de 320x120 que é enviada ao LCD
uint16_t*   g_buf = nullptr;
int         g_bandY = 0;

uint32_t g_now = 0, g_lastFrame = 0;
float    g_dt = 0.016f;
float    g_fps = 0;
float    g_fpsHist[48];
uint32_t g_fpsLastPush = 0;

// preferências
int   g_accent = 0;
bool  g_anim = true;
bool  g_showFps = false;
float g_brightness = 200;

// transição entre telas
float g_fade = 1.0f;

lgfx::VLWfont         F12, F14, F20, F30, F52;
lgfx::PointerWrapper  W12, W14, W20, W30, W52;

inline uint32_t accent() { return ACCENTS[g_accent]; }

// ---------------------------------------------------------------------
//  Utilidades matemáticas / cor
// ---------------------------------------------------------------------
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Aproxima "v" de "target" de forma exponencial (independe do FPS).
inline void approach(float& v, float target, float speed)
{
  if (!g_anim) { v = target; return; }
  v += (target - v) * (1.0f - expf(-speed * g_dt));
  if (fabsf(target - v) < 0.0005f) v = target;
}

inline float easeOut(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }

uint32_t mix(uint32_t a, uint32_t b, float t)
{
  t = clampf(t, 0, 1);
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  uint32_t r = (uint32_t)(ar + (br - ar) * t);
  uint32_t g = (uint32_t)(ag + (bg - ag) * t);
  uint32_t bl = (uint32_t)(ab + (bb - ab) * t);
  return (r << 16) | (g << 8) | bl;
}

// Aplica o efeito de "fade" da transição de tela em qualquer cor.
inline uint32_t C(uint32_t c) { return g_fade >= 0.999f ? c : mix(COL_BG, c, g_fade); }

// gerador pseudo-aleatório simples (igual no ESP32 e no PC)
uint32_t g_rng = 0x12345678;
inline float frand()
{
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return (g_rng & 0xFFFFFF) / 16777216.0f;
}
inline float grand() { return (frand() + frand() + frand() - 1.5f) * 1.15f; }

// ---------------------------------------------------------------------
//  Atalhos de desenho (todos passam pela função C() do fade)
// ---------------------------------------------------------------------
LGFX_Sprite& G = g_canvas;

inline bool inBand(int y, int h) { return y < g_bandY + BAND_H && y + h > g_bandY; }

struct ClipRect { int x, y, w, h; } g_clip;

bool clip(int x, int y, int w, int h)
{
  int y0 = y > g_bandY ? y : g_bandY;
  int y1 = (y + h) < (g_bandY + BAND_H) ? (y + h) : (g_bandY + BAND_H);
  if (y1 <= y0) return false;
  g_clip = { x, y0, w, y1 - y0 };
  G.setClipRect(x, y0, w, y1 - y0);
  return true;
}
inline void unclip() { g_clip = { 0, g_bandY, W, BAND_H }; G.setClipRect(0, g_bandY, W, BAND_H); }

// Linha suavizada (anti-aliased) com espessura variável (r0 -> r1).
// Feita aqui porque a drawWideLine da LovyanGFX ignora o recorte (clip),
// e nós dependemos do recorte para desenhar em faixas.
void wedge(float ax, float ay, float bx, float by, float ar, float br, uint32_t col)
{
  int x0 = (int)floorf(fminf(ax - ar, bx - br)) - 1, x1 = (int)ceilf(fmaxf(ax + ar, bx + br)) + 1;
  int y0 = (int)floorf(fminf(ay - ar, by - br)) - 1, y1 = (int)ceilf(fmaxf(ay + ar, by + br)) + 1;
  if (x0 < g_clip.x) x0 = g_clip.x;
  if (y0 < g_clip.y) y0 = g_clip.y;
  if (x1 > g_clip.x + g_clip.w - 1) x1 = g_clip.x + g_clip.w - 1;
  if (y1 > g_clip.y + g_clip.h - 1) y1 = g_clip.y + g_clip.h - 1;
  if (x0 > x1 || y0 > y1) return;
  float bax = bx - ax, bay = by - ay, len2 = bax * bax + bay * bay;
  float inv = len2 > 0 ? 1.0f / len2 : 0;
  for (int y = y0; y <= y1; y++) {
    bool inside = false;
    for (int x = x0; x <= x1; x++) {
      float px = x - ax, py = y - ay;
      float h = clampf((px * bax + py * bay) * inv, 0, 1);
      float dx = px - bax * h, dy = py - bay * h;
      float d = sqrtf(dx * dx + dy * dy) - (ar + (br - ar) * h);
      float a = 0.5f - d;
      if (a <= 0.02f) { if (inside) break; continue; }   // já passou da linha nesta fileira
      inside = true;
      if (a >= 0.98f) G.drawPixel(x, y, col);
      else G.fillRectAlpha(x, y, 1, 1, (uint8_t)(a * 255), col);
    }
  }
}

inline void rrect(int x, int y, int w, int h, int r, uint32_t c)
{
  if (inBand(y, h)) G.fillSmoothRoundRect(x, y, w, h, r, C(c));
}
// cartão com borda de 1px
inline void card(int x, int y, int w, int h, int r, uint32_t fill, uint32_t border = COL_LINE)
{
  if (!inBand(y, h)) return;
  G.fillSmoothRoundRect(x, y, w, h, r, C(border));
  G.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, C(fill));
}
inline void circle(int x, int y, int r, uint32_t c)
{
  if (inBand(y - r, 2 * r + 1)) G.fillSmoothCircle(x, y, r, C(c));
}
inline void line(float x0, float y0, float x1, float y1, float r, uint32_t c)
{
  wedge(x0, y0, x1, y1, r, r, C(c));
}

int text(const char* s, int x, int y, const lgfx::IFont* f, uint32_t c, textdatum_t d = textdatum_t::middle_left)
{
  G.setFont(f);
  G.setTextDatum(d);
  G.setTextColor(C(c));
  return G.drawString(s, x, y);
}
inline int textW(const char* s, const lgfx::IFont* f) { G.setFont(f); return G.textWidth(s); }

// ---------------------------------------------------------------------
//  Entrada (touch)
// ---------------------------------------------------------------------
struct TouchState {
  bool down = false;       // dedo na tela agora
  bool pressed = false;    // acabou de encostar
  bool released = false;   // acabou de soltar
  int  x = 0, y = 0;       // última posição
  int  sx = 0, sy = 0;     // onde encostou
} T;

int   g_active = -1;       // id do widget que "capturou" o toque
constexpr int N_IDS = 80;
float g_press[N_IDS];         // animação de pressionado por widget (0..1)
int   g_upFrames = 0;

inline bool inRect(int px, int py, int x, int y, int w, int h, int m = 4)
{
  return px >= x - m && px < x + w + m && py >= y - m && py < y + h + m;
}

void readTouch()
{
  int16_t x, y;
  bool now = plat_get_touch(&x, &y);
  T.pressed = T.released = false;
  if (now) {
    g_upFrames = 0;
    T.x = x; T.y = y;
    if (!T.down) { T.down = true; T.pressed = true; T.sx = x; T.sy = y; }
  } else if (T.down) {
    // pequeno "debounce": só considera solto após 2 leituras sem toque
    if (++g_upFrames >= 2) { T.down = false; T.released = true; }
  }
}

// Botão genérico: chame na fase de update. Retorna true no clique.
bool button(int id, int x, int y, int w, int h)
{
  if (T.pressed && inRect(T.x, T.y, x, y, w, h)) g_active = id;
  return T.released && g_active == id && inRect(T.x, T.y, x, y, w, h, 12);
}

// ---------------------------------------------------------------------
//  Simulação de sensores
// ---------------------------------------------------------------------
constexpr int HIST = 64;
struct Series {
  const char* name;
  const char* shortName;
  const char* unit;
  uint32_t color;
  int   dec;
  float base, amp, period, noise;
  float r = 0;              // ruído acumulado
  float spike = 0;          // picos (para mostrar a auto-escala)
  float v = 0;
  float shown = 0;          // valor exibido (animado)
  float hist[HIST];
  int   count = 0;
};

Series S[4] = {
  { "Temperatura", "Temp",    "°C",  0xFB923C, 1, 24.5f,  2.2f,  40, 0.12f },
  { "Umidade",     "Umid.",   "%",   0x38BDF8, 0, 58.0f,  9.0f,  60, 0.7f  },
  { "Pressão",     "Pressão", "hPa", 0xA78BFA, 1, 1013.f, 2.5f,  90, 0.10f },
  { "Uso de CPU",  "CPU",     "%",   0x34D399, 0, 32.0f,  6.0f,  25, 2.5f  },
};
uint32_t g_lastSample = 0;
constexpr uint32_t SAMPLE_MS = 350;
float    g_sampleFrac = 0;      // 0..1 entre amostras (rolagem suave)

void sampleSeries()
{
  float t = g_now / 1000.0f;
  for (int i = 0; i < 4; i++) {
    Series& s = S[i];
    s.r = s.r * 0.9f + grand() * s.noise;
    float v = s.base + s.amp * sinf(2 * PI_F * t / s.period) + s.r;
    if (i == 3) { // CPU: de vez em quando um pico de carga
      if (s.spike < 1 && frand() < 0.025f) s.spike = 25 + frand() * 40;
      s.spike *= 0.93f;
      v += s.spike;
      v = clampf(v, 2, 100);
    }
    if (i == 1) v = clampf(v, 0, 100);
    s.v = v;
    if (s.count < HIST) s.hist[s.count++] = v;
    else { memmove(s.hist, s.hist + 1, (HIST - 1) * sizeof(float)); s.hist[HIST - 1] = v; }
  }
}

void initSeries()
{
  // pré-preenche o histórico para os gráficos já começarem cheios
  uint32_t real = g_now;
  for (int k = 0; k < HIST; k++) { g_now = real - (HIST - k) * SAMPLE_MS; sampleSeries(); }
  g_now = real;
  for (auto& s : S) s.shown = s.v;
}

void fmtVal(char* buf, size_t n, float v, int dec)
{
  if (dec == 0) snprintf(buf, n, "%d", (int)lroundf(v));
  else snprintf(buf, n, "%.*f", dec, v);
}

// ---------------------------------------------------------------------
//  Relógio (hora inicial = hora da compilação, ajustável na tela)
// ---------------------------------------------------------------------
int32_t g_timeBase = 0;
int32_t secondsOfDay()
{
  int32_t s = (g_timeBase + (int32_t)(g_now / 1000)) % 86400;
  if (s < 0) s += 86400;
  return s;
}

// ---------------------------------------------------------------------
//  Telas
// ---------------------------------------------------------------------
enum Screen { SCR_HOME, SCR_DASH, SCR_CHART, SCR_CLOCK, SCR_GAUGE, SCR_SETTINGS, SCR_SYSTEM };
Screen g_screen = SCR_HOME;

const char* screenTitle(Screen s)
{
  switch (s) {
    case SCR_DASH:     return "Dashboard";
    case SCR_CHART:    return "Gráficos";
    case SCR_CLOCK:    return "Relógio";
    case SCR_GAUGE:    return "Medidores";
    case SCR_SETTINGS: return "Ajustes";
    case SCR_SYSTEM:   return "Sistema";
    default:           return "Painel ESP32";
  }
}

void goTo(Screen s)
{
  g_screen = s;
  g_fade = g_anim ? 0.0f : 1.0f;
  g_active = -1;
  for (auto& p : g_press) p = 0;
}

// ---------------------------------------------------------------------
//  Ícones vetoriais (desenhados com primitivas, sem imagens)
// ---------------------------------------------------------------------
void drawIcon(int id, int cx, int cy, uint32_t c, uint32_t bg)
{
  switch (id) {
    case 0: // dashboard: 4 quadrados
      for (int i = 0; i < 4; i++) {
        int x = cx - 11 + (i & 1) * 12, y = cy - 11 + (i >> 1) * 12;
        G.fillSmoothRoundRect(x, y, 10, 10, 3, C(i == 1 ? c : mix(bg, c, 0.65f)));
      }
      break;
    case 1: { // gráfico de linha
      const int px[] = { -11, -5, 0, 5, 11 }, py[] = { 7, 0, 3, -5, -9 };
      for (int i = 0; i < 4; i++) line(cx + px[i], cy + py[i], cx + px[i + 1], cy + py[i + 1], 1.4f, c);
      G.fillSmoothCircle(cx + 11, cy - 9, 3, C(c));
      line(cx - 12, cy + 11, cx + 12, cy + 11, 0.8f, mix(bg, c, 0.5f));
      break;
    }
    case 2: { // relógio
      G.fillSmoothCircle(cx, cy, 12, C(c));
      G.fillSmoothCircle(cx, cy, 10, C(bg));
      line(cx, cy, cx, cy - 7, 1.2f, c);
      line(cx, cy, cx + 5, cy + 2, 1.2f, c);
      G.fillSmoothCircle(cx, cy, 2, C(c));
      break;
    }
    case 3: { // medidor
      G.fillArc(cx, cy + 3, 9, 12, 180, 360, C(mix(bg, c, 0.45f)));
      G.fillArc(cx, cy + 3, 9, 12, 180, 290, C(c));
      line(cx, cy + 3, cx + 6, cy - 4, 1.3f, c);
      G.fillSmoothCircle(cx, cy + 3, 2, C(c));
      break;
    }
    case 4: { // engrenagem
      for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4;
        line(cx + cosf(a) * 7, cy + sinf(a) * 7, cx + cosf(a) * 12, cy + sinf(a) * 12, 2.2f, c);
      }
      G.fillSmoothCircle(cx, cy, 9, C(c));
      G.fillSmoothCircle(cx, cy, 4, C(bg));
      break;
    }
    case 5: { // chip
      for (int i = -1; i <= 1; i++) {
        line(cx + i * 5, cy - 13, cx + i * 5, cy + 13, 1.0f, c);
        line(cx - 13, cy + i * 5, cx + 13, cy + i * 5, 1.0f, c);
      }
      G.fillSmoothRoundRect(cx - 9, cy - 9, 18, 18, 4, C(c));
      G.fillSmoothRoundRect(cx - 6, cy - 6, 12, 12, 2, C(bg));
      G.fillSmoothRoundRect(cx - 3, cy - 3, 6, 6, 1, C(c));
      break;
    }
  }
}

// ---------------------------------------------------------------------
//  Barra de status (topo)
// ---------------------------------------------------------------------
constexpr int ID_BACK = 1;

void updateStatusBar()
{
  if (g_screen != SCR_HOME && button(ID_BACK, 0, 0, 120, BAR_H)) goTo(SCR_HOME);
}

void drawStatusBar()
{
  if (!inBand(0, BAR_H)) return;
  int x = 10;
  if (g_screen != SCR_HOME) {
    float p = g_press[ID_BACK];
    if (p > 0.01f) circle(16, 14, 12, mix(COL_BG, accent(), 0.25f * p));
    line(19, 8, 13, 14, 1.3f, COL_TEXT);
    line(13, 14, 19, 20, 1.3f, COL_TEXT);
    x = 32;
  } else {
    circle(16, 14, 4, accent());
    x = 26;
  }
  text(screenTitle(g_screen), x, 14, &F14, COL_TEXT);

  // hora + "pulso" ao vivo
  char buf[16];
  int32_t s = secondsOfDay();
  snprintf(buf, sizeof(buf), "%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60));
  int tw = text(buf, W - 10, 14, &F14, COL_TEXT, textdatum_t::middle_right);
  float pulse = 0.5f + 0.5f * sinf(g_now * 0.006f);
  circle(W - 18 - tw, 14, 3, mix(COL_BG, COL_GOOD, 0.35f + 0.65f * pulse));

  if (g_showFps) {
    snprintf(buf, sizeof(buf), "%d fps", (int)lroundf(g_fps));
    text(buf, W - 30 - tw, 14, &F12, COL_MUTED, textdatum_t::middle_right);
  }
  G.drawFastHLine(0, BAR_H, W, C(COL_LINE));
}

// =====================================================================
//  TELA: INÍCIO
// =====================================================================
struct Tile { const char* label; const char* sub; int icon; uint32_t color; Screen target; };
const Tile TILES[6] = {
  { "Dashboard", "4 sensores",  0, 0x22D3EE, SCR_DASH     },
  { "Gráficos",  "auto-escala", 1, 0xA78BFA, SCR_CHART    },
  { "Relógio",   "analógico",   2, 0x34D399, SCR_CLOCK    },
  { "Medidores", "interativo",  3, 0xFBBF24, SCR_GAUGE    },
  { "Ajustes",   "tema e touch",4, 0xF472B6, SCR_SETTINGS },
  { "Sistema",   "memória/FPS", 5, 0x60A5FA, SCR_SYSTEM   },
};
constexpr int TILE_W = 96, TILE_H = 98, ID_TILE0 = 10;

struct Ripple { int tile = -1; int x, y; uint32_t t0; } g_ripple;

inline int tileX(int i) { return 8 + (i % 3) * (TILE_W + 8); }
inline int tileY(int i) { return BAR_H + 6 + (i / 3) * (TILE_H + 6); }

void updateHome()
{
  if (g_ripple.tile >= 0) {
    // espera a "onda" terminar para trocar de tela
    if (!g_anim || g_now - g_ripple.t0 > 260) {
      Screen t = TILES[g_ripple.tile].target;
      g_ripple.tile = -1;
      goTo(t);
    }
    return;
  }
  for (int i = 0; i < 6; i++) {
    if (button(ID_TILE0 + i, tileX(i), tileY(i), TILE_W, TILE_H)) {
      g_ripple.tile = i; g_ripple.x = T.x; g_ripple.y = T.y; g_ripple.t0 = g_now;
    }
  }
}

void drawHome()
{
  for (int i = 0; i < 6; i++) {
    const Tile& t = TILES[i];
    int x = tileX(i), y = tileY(i);
    if (!inBand(y, TILE_H)) continue;
    float p = g_press[ID_TILE0 + i];
    uint32_t bg = mix(COL_CARD, t.color, 0.10f * p);
    card(x, y, TILE_W, TILE_H, 14, bg, mix(COL_LINE, t.color, 0.6f * p));

    // efeito "ripple": círculo que se expande a partir do dedo
    if (g_ripple.tile == i) {
      float k = clampf((g_now - g_ripple.t0) / 260.0f, 0, 1);
      if (clip(x + 3, y + 3, TILE_W - 6, TILE_H - 6)) {
        G.fillSmoothCircle(g_ripple.x, g_ripple.y, (int)(10 + 120 * easeOut(k)),
                           C(mix(bg, t.color, 0.28f * (1 - k))));
        unclip();
      }
    }

    int cx = x + TILE_W / 2, cy = y + 36;
    float glow = 0.16f + 0.10f * p;
    circle(cx, cy, 22, mix(bg, t.color, glow));
    drawIcon(t.icon, cx, cy, t.color, mix(bg, t.color, glow));
    text(t.label, cx, y + 71, &F14, COL_TEXT, textdatum_t::middle_center);
    text(t.sub, cx, y + 87, &F12, COL_MUTED, textdatum_t::middle_center);
  }
}

// =====================================================================
//  TELA: DASHBOARD  (4 cartões com valor animado + mini-gráfico)
// =====================================================================
constexpr int ID_CARD0 = 20;
int g_chartSeries = 0;   // série escolhida para a tela de gráficos

inline int dashX(int i) { return 8 + (i % 2) * 156; }
inline int dashY(int i) { return BAR_H + 6 + (i / 2) * 103; }

void updateDash()
{
  for (int i = 0; i < 4; i++)
    if (button(ID_CARD0 + i, dashX(i), dashY(i), 148, 97)) { g_chartSeries = i; goTo(SCR_CHART); }
}

void sparkline(const Series& s, int x, int y, int w, int h, int n, uint32_t bg)
{
  int first = s.count - n; if (first < 0) first = 0;
  int cnt = s.count - first;
  if (cnt < 2) return;
  float lo = 1e9f, hi = -1e9f;
  for (int i = first; i < s.count; i++) { lo = fminf(lo, s.hist[i]); hi = fmaxf(hi, s.hist[i]); }
  float pad = (hi - lo) * 0.15f + 0.001f;
  lo -= pad; hi += pad;
  float dx = (float)w / (n - 1);
  float shift = g_sampleFrac * dx;          // rolagem suave entre amostras
  auto Y = [&](float v) { return y + h - (v - lo) / (hi - lo) * h; };

  if (!clip(x, y - 4, w, h + 6)) return;
  uint32_t top = mix(bg, s.color, 0.40f);
  // preenchimento em degradê sob a curva
  for (int i = 0; i < cnt - 1; i++) {
    float x0 = x + i * dx - shift, x1 = x0 + dx;
    float y0 = Y(s.hist[first + i]), y1 = Y(s.hist[first + i + 1]);
    for (int px = (int)ceilf(x0); px < (int)ceilf(x1); px++) {
      float yy = lerpf(y0, y1, (px - x0) / dx);
      int len = y + h - (int)yy;
      if (len > 0) G.drawGradientVLine(px, (int)yy, len, C(top), C(bg));
    }
  }
  for (int i = 0; i < cnt - 1; i++)
    line(x + i * dx - shift, Y(s.hist[first + i]), x + (i + 1) * dx - shift, Y(s.hist[first + i + 1]), 0.9f, s.color);
  unclip();
  float ly = Y(s.hist[s.count - 1]);
  float lx = x + (cnt - 1) * dx - shift;
  if (inBand((int)ly - 6, 12)) {
    G.fillSmoothCircle((int)lx, (int)ly, 5, C(mix(bg, s.color, 0.35f)));
    G.fillSmoothCircle((int)lx, (int)ly, 3, C(s.color));
  }
}

void drawDash()
{
  char buf[24];
  for (int i = 0; i < 4; i++) {
    Series& s = S[i];
    int x = dashX(i), y = dashY(i), w = 148, h = 97;
    if (!inBand(y, h)) continue;
    float p = g_press[ID_CARD0 + i];
    uint32_t bg = mix(COL_CARD, s.color, 0.08f * p);
    card(x, y, w, h, 12, bg, mix(COL_LINE, s.color, 0.55f * p));

    circle(x + 13, y + 14, 3, s.color);
    text(s.name, x + 22, y + 14, &F12, COL_MUTED);

    // tendência (compara com ~3 s atrás)
    float d = s.hist[s.count - 1] - s.hist[s.count - 9];
    bool up = d >= 0;
    uint32_t dc = up ? COL_GOOD : COL_BAD;
    fmtVal(buf, sizeof(buf), fabsf(d), s.dec == 0 ? 0 : 1);
    int tw = text(buf, x + w - 10, y + 14, &F12, dc, textdatum_t::middle_right);
    int ax = x + w - 18 - tw, ay = y + 14;
    if (up) G.fillTriangle(ax - 4, ay + 3, ax + 4, ay + 3, ax, ay - 3, C(dc));
    else    G.fillTriangle(ax - 4, ay - 3, ax + 4, ay - 3, ax, ay + 3, C(dc));

    // valor grande animado
    fmtVal(buf, sizeof(buf), s.shown, s.dec);
    int vw = text(buf, x + 12, y + 38, &F20, COL_TEXT);
    text(s.unit, x + 15 + vw, y + 41, &F12, COL_MUTED);

    sparkline(s, x + 10, y + 58, w - 20, h - 66, 28, bg);
  }
}

// =====================================================================
//  TELA: GRÁFICOS  (linha/barras, auto-escala animada, cursor no toque)
// =====================================================================
constexpr int ID_CHIP0 = 30, ID_MODE = 36, ID_PLOT = 37;
bool  g_barMode = false;
float g_modeAnim = 0;
float g_axMin = 0, g_axMax = 1;
bool  g_axInit = false;
int   g_lastAxSeries = -1;

constexpr int CH_X = 8, CH_Y = BAR_H + 34, CH_W = 304, CH_H = H - BAR_H - 34 - 6;
constexpr int PX0 = CH_X + 46, PX1 = CH_X + CH_W - 14;
constexpr int PY0 = CH_Y + 42, PY1 = CH_Y + CH_H - 24;

float niceStep(float range, int ticks)
{
  float raw = range / ticks;
  if (raw <= 0) return 1;
  float mag = powf(10, floorf(log10f(raw)));
  float f = raw / mag;
  float nf = f < 1.5f ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10;
  return nf * mag;
}

int chipX(int i) { return 8 + i * 61; }

void visibleRange(const Series& s, int n, float& lo, float& hi)
{
  lo = 1e9f; hi = -1e9f;
  for (int i = s.count - n; i < s.count; i++) { lo = fminf(lo, s.hist[i]); hi = fmaxf(hi, s.hist[i]); }
}

void updateChart()
{
  for (int i = 0; i < 4; i++)
    if (button(ID_CHIP0 + i, chipX(i), BAR_H + 6, 57, 22)) g_chartSeries = i;
  if (button(ID_MODE, 252, BAR_H + 6, 60, 22)) g_barMode = !g_barMode;
  approach(g_modeAnim, g_barMode ? 1 : 0, 14);

  if (T.pressed && inRect(T.x, T.y, PX0, PY0, PX1 - PX0, PY1 - PY0, 8)) g_active = ID_PLOT;

  // auto-escala: calcula a faixa "bonita" alvo e anima até ela
  const Series& s = S[g_chartSeries];
  int n = g_barMode ? 24 : HIST;
  float lo, hi; visibleRange(s, n, lo, hi);
  float span = fmaxf(hi - lo, fabsf(hi) * 0.002f + 0.5f);
  float step = niceStep(span * 1.2f, 4);
  float tMin = floorf((lo - span * 0.1f) / step) * step;
  float tMax = ceilf((hi + span * 0.1f) / step) * step;
  if (g_barMode && lo >= 0 && tMin > 0 && tMin < span * 2) tMin = 0;
  if (!g_axInit || g_lastAxSeries != g_chartSeries) {
    if (!g_axInit) { g_axMin = tMin; g_axMax = tMax; }
    g_axInit = true; g_lastAxSeries = g_chartSeries;
  }
  approach(g_axMin, tMin, 5);
  approach(g_axMax, tMax, 5);
}

void drawChart()
{
  char buf[32];
  const Series& s = S[g_chartSeries];

  // chips de seleção de série
  for (int i = 0; i < 4; i++) {
    int x = chipX(i), y = BAR_H + 6;
    if (!inBand(y, 22)) break;
    bool sel = i == g_chartSeries;
    float p = g_press[ID_CHIP0 + i];
    uint32_t c = S[i].color;
    uint32_t fill = sel ? mix(COL_BG, c, 0.22f) : mix(COL_BG, COL_CARD2, 0.6f + 0.4f * p);
    card(x, y, 57, 22, 11, fill, sel ? mix(COL_BG, c, 0.7f) : COL_LINE);
    text(S[i].shortName, x + 28, y + 11, &F12, sel ? COL_WHITE : COL_MUTED, textdatum_t::middle_center);
  }
  // botão Linha/Barras (segmentado)
  {
    int x = 252, y = BAR_H + 6;
    if (inBand(y, 22)) {
      card(x, y, 60, 22, 11, mix(COL_CARD2, accent(), 0.15f * g_press[ID_MODE]));
      int kx = (int)(x + 2 + g_modeAnim * 28);
      G.fillSmoothRoundRect(kx, y + 2, 28, 18, 9, C(mix(COL_CARD2, accent(), 0.45f)));
      // ícone linha
      uint32_t c0 = mix(COL_MUTED, COL_WHITE, 1 - g_modeAnim);
      line(x + 9, y + 15, x + 13, y + 9, 1.0f, c0);
      line(x + 13, y + 9, x + 17, y + 12, 1.0f, c0);
      line(x + 17, y + 12, x + 22, y + 6, 1.0f, c0);
      // ícone barras
      uint32_t c1 = mix(COL_MUTED, COL_WHITE, g_modeAnim);
      G.fillRect(x + 36, y + 11, 3, 6, C(c1));
      G.fillRect(x + 41, y + 7, 3, 10, C(c1));
      G.fillRect(x + 46, y + 9, 3, 8, C(c1));
      G.fillRect(x + 51, y + 5, 3, 12, C(c1));
    }
  }

  card(CH_X, CH_Y, CH_W, CH_H, 12, COL_CARD);

  // cabeçalho: valor atual + mín/máx
  int n = g_barMode ? 24 : HIST;
  float lo, hi; visibleRange(s, n, lo, hi);
  if (inBand(CH_Y, 40)) {
    fmtVal(buf, sizeof(buf), s.shown, s.dec);
    int vw = text(buf, CH_X + 12, CH_Y + 20, &F20, COL_TEXT);
    text(s.unit, CH_X + 15 + vw, CH_Y + 23, &F12, COL_MUTED);
    char a[12], b[12];
    fmtVal(a, sizeof(a), lo, s.dec); fmtVal(b, sizeof(b), hi, s.dec);
    snprintf(buf, sizeof(buf), "mín %s  ·  máx %s", a, b);
    text(buf, CH_X + CH_W - 12, CH_Y + 20, &F12, COL_MUTED, textdatum_t::middle_right);
  }

  // grade + rótulos do eixo Y (acompanham a escala animada)
  float range = fmaxf(g_axMax - g_axMin, 1e-3f);
  auto Y = [&](float v) { return PY1 - (v - g_axMin) / range * (PY1 - PY0); };
  float step = niceStep(range, 4);
  int dec = step < 1 ? (step < 0.1f ? 2 : 1) : 0;
  for (float v = ceilf(g_axMin / step) * step; v <= g_axMax + step * 0.01f; v += step) {
    int y = (int)lroundf(Y(v));
    if (y < PY0 - 1 || y > PY1 + 1) continue;
    for (int x = PX0; x < PX1; x += 6) G.drawFastHLine(x, y, 3, C(COL_LINE));
    snprintf(buf, sizeof(buf), "%.*f", dec, v);
    text(buf, PX0 - 6, y, &F12, COL_DIM, textdatum_t::middle_right);
  }
  float secs = n * SAMPLE_MS / 1000.0f;
  snprintf(buf, sizeof(buf), "-%.0f s", secs);
  text(buf, PX0, PY1 + 13, &F12, COL_DIM);
  text("agora", PX1, PY1 + 13, &F12, COL_DIM, textdatum_t::middle_right);

  float dx = (float)(PX1 - PX0) / (n - 1);
  float shift = g_sampleFrac * dx;
  int first = s.count - n;

  if (clip(PX0, PY0 - 6, PX1 - PX0 + 8, PY1 - PY0 + 7)) {
    if (!g_barMode) {
      uint32_t top = mix(COL_CARD, s.color, 0.45f);
      for (int i = 0; i < n - 1; i++) {
        float x0 = PX0 + i * dx - shift, y0 = Y(s.hist[first + i]), y1 = Y(s.hist[first + i + 1]);
        for (int px = (int)ceilf(x0); px < (int)ceilf(x0 + dx); px++) {
          if (px < PX0) continue;
          float yy = lerpf(y0, y1, (px - x0) / dx);
          int len = PY1 - (int)yy;
          if (len > 0) G.drawGradientVLine(px, (int)yy, len, C(top), C(COL_CARD));
        }
      }
      for (int i = 0; i < n - 1; i++)
        line(PX0 + i * dx - shift, Y(s.hist[first + i]), PX0 + (i + 1) * dx - shift, Y(s.hist[first + i + 1]), 1.1f, s.color);
    } else {
      float base = Y(fmaxf(g_axMin, fminf(0.0f, g_axMax)));
      if (g_axMin > 0) base = PY1;
      for (int i = 0; i < n; i++) {
        float v = s.hist[first + i];
        float x = PX0 + i * dx - shift - dx * 0.5f;
        float y = Y(v);
        int bh = (int)(base - y);
        if (bh < 2) bh = 2;
        float k = (float)i / (n - 1);
        uint32_t c = (i == n - 1) ? s.color : mix(mix(COL_CARD, s.color, 0.30f), s.color, k * 0.75f);
        G.fillSmoothRoundRect((int)(x + 2), (int)(base - bh), (int)(dx - 4), bh, 3, C(c));
      }
    }
    unclip();
  }

  // ponto final + "balão" com o valor
  if (!g_barMode) {
    float lx = PX0 + (n - 1) * dx - shift, ly = Y(s.hist[s.count - 1]);
    if (inBand((int)ly - 8, 16)) {
      float pulse = 0.5f + 0.5f * sinf(g_now * 0.008f);
      G.fillSmoothCircle((int)lx, (int)ly, (int)(5 + 3 * pulse), C(mix(COL_CARD, s.color, 0.25f)));
      G.fillSmoothCircle((int)lx, (int)ly, 3, C(s.color));
    }
  }

  // cursor de inspeção enquanto o dedo está sobre o gráfico
  if (g_active == ID_PLOT && T.down) {
    int idx = (int)lroundf((T.x - PX0 + shift) / dx);
    if (idx < 0) idx = 0;
    if (idx > n - 1) idx = n - 1;
    float v = s.hist[first + idx];
    float x = PX0 + idx * dx - shift, y = Y(v);
    for (int yy = PY0; yy < PY1; yy += 4) G.drawFastVLine((int)x, yy, 2, C(COL_MUTED));
    G.fillSmoothCircle((int)x, (int)y, 5, C(COL_WHITE));
    G.fillSmoothCircle((int)x, (int)y, 3, C(s.color));
    char vb[16]; fmtVal(vb, sizeof(vb), v, s.dec);
    snprintf(buf, sizeof(buf), "%s %s", vb, s.unit);
    int tw = textW(buf, &F14) + 16;
    int bx = (int)x - tw / 2;
    if (bx < PX0) bx = PX0;
    if (bx + tw > PX1) bx = PX1 - tw;
    int by = (int)y - 34; if (by < PY0 - 4) by = (int)y + 12;
    card(bx, by, tw, 24, 8, COL_CARD2, mix(COL_LINE, s.color, 0.6f));
    text(buf, bx + tw / 2, by + 12, &F14, COL_TEXT, textdatum_t::middle_center);
    float ago = (n - 1 - idx) * SAMPLE_MS / 1000.0f;
    snprintf(buf, sizeof(buf), "-%.1f s", ago);
    if (inBand(PY1 + 6, 14)) {
      G.fillRect(PX0, PY1 + 5, PX1 - PX0, 16, C(COL_CARD));
      text(buf, (int)x, PY1 + 13, &F12, COL_TEXT, textdatum_t::middle_center);
    }
  }
}

// =====================================================================
//  TELA: RELÓGIO  (analógico com ponteiro de segundos suave / digital)
// =====================================================================
constexpr int ID_CLOCKFACE = 40, ID_HMINUS = 41, ID_HPLUS = 42, ID_MMINUS = 43, ID_MPLUS = 44;
bool  g_digital = false;
float g_digAnim = 0;

void updateClock()
{
  if (button(ID_CLOCKFACE, 4, BAR_H + 4, 172, H - BAR_H - 8)) g_digital = !g_digital;
  if (button(ID_HMINUS, 192, 168, 54, 28)) g_timeBase -= 3600;
  if (button(ID_HPLUS,  252, 168, 54, 28)) g_timeBase += 3600;
  if (button(ID_MMINUS, 192, 202, 54, 28)) g_timeBase -= 60;
  if (button(ID_MPLUS,  252, 202, 54, 28)) g_timeBase += 60;
  approach(g_digAnim, g_digital ? 1 : 0, 10);
}

void pillButton(int id, int x, int y, int w, int h, const char* label)
{
  if (!inBand(y, h)) return;
  float p = g_press[id];
  card(x, y, w, h, h / 2, mix(COL_CARD2, accent(), 0.35f * p), mix(COL_LINE, accent(), 0.8f * p));
  text(label, x + w / 2, y + h / 2, &F14, mix(COL_TEXT, COL_WHITE, p), textdatum_t::middle_center);
}

void drawClock()
{
  char buf[24];
  int32_t sec = secondsOfDay();
  float frac = (g_now % 1000) / 1000.0f;
  int hh = sec / 3600, mm = sec / 60 % 60, ss = sec % 60;
  const int cx = 90, cy = 134, R = 84;
  float d = g_digAnim;

  if (d < 0.99f) {
    // ---- analógico ----
    float a = 1 - d;
    uint32_t face = mix(COL_BG, COL_CARD, a);
    circle(cx, cy, R, mix(COL_BG, COL_LINE, a));
    circle(cx, cy, R - 1, face);
    for (int i = 0; i < 60; i++) {
      float ang = i * 6 * PI_F / 180;
      float cs = cosf(ang), sn = sinf(ang);
      bool hour = i % 5 == 0;
      float r0 = hour ? R - 13 : R - 7, r1 = R - 5;
      line(cx + cs * r0, cy + sn * r0, cx + cs * r1, cy + sn * r1, hour ? 1.3f : 0.6f,
           mix(face, hour ? COL_TEXT : COL_DIM, a));
    }
    const char* nums[] = { "12", "3", "6", "9" };
    for (int i = 0; i < 4; i++) {
      float ang = (i * 90 - 90) * PI_F / 180;
      text(nums[i], cx + (int)(cosf(ang) * (R - 25)), cy + (int)(sinf(ang) * (R - 25)), &F14,
           mix(face, COL_MUTED, a), textdatum_t::middle_center);
    }
    float sA = (ss + easeOut(clampf(frac * 4, 0, 1))) * 6;      // "tique" suave
    float mA = (mm + ss / 60.0f) * 6;
    float hA = ((hh % 12) + mm / 60.0f) * 30;
    auto hand = [&](float deg, float len, float w0, float w1, uint32_t c) {
      float r = (deg - 90) * PI_F / 180;
      wedge(cx, cy, cx + cosf(r) * len, cy + sinf(r) * len, w0, w1, C(mix(face, c, a)));
    };
    hand(hA, R * 0.50f, 3.5f, 1.8f, COL_TEXT);
    hand(mA, R * 0.75f, 2.6f, 1.2f, COL_TEXT);
    float sr = (sA - 90) * PI_F / 180;
    line(cx - cosf(sr) * 14, cy - sinf(sr) * 14, cx + cosf(sr) * (R - 10), cy + sinf(sr) * (R - 10), 0.9f, mix(face, accent(), a));
    circle(cx, cy, 5, mix(face, accent(), a));
    circle(cx, cy, 2, face);
  }
  if (d > 0.01f) {
    // ---- digital ----
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    text(buf, cx, cy - 10, &F52, mix(COL_BG, COL_TEXT, d), textdatum_t::middle_center);
    // anel de segundos
    float prog = (ss + frac) / 60.0f;
    int barW = 140, bx = cx - barW / 2, by = cy + 30;
    rrect(bx, by, barW, 6, 3, mix(COL_BG, COL_LINE, d));
    rrect(bx, by, (int)(6 + (barW - 6) * prog), 6, 3, mix(COL_BG, accent(), d));
    snprintf(buf, sizeof(buf), "%02d s", ss);
    text(buf, cx, by + 22, &F14, mix(COL_BG, COL_MUTED, d), textdatum_t::middle_center);
  }

  // ---- painel da direita ----
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  int tw = text(buf, 192, BAR_H + 28, &F30, COL_TEXT);
  snprintf(buf, sizeof(buf), ":%02d", ss);
  text(buf, 194 + tw, BAR_H + 32, &F14, accent());

  uint32_t up = g_now / 1000;
  snprintf(buf, sizeof(buf), "%uh %02um %02us", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60));
  text("Tempo ligado", 192, BAR_H + 62, &F12, COL_MUTED);
  text(buf, 192, BAR_H + 80, &F14, COL_TEXT);
  text(g_digital ? "Toque: analógico" : "Toque: digital", 192, BAR_H + 108, &F12, COL_DIM);

  text("Ajustar hora / minuto", 192, 156, &F12, COL_MUTED);
  pillButton(ID_HMINUS, 192, 168, 54, 28, "− h");
  pillButton(ID_HPLUS,  252, 168, 54, 28, "+ h");
  pillButton(ID_MMINUS, 192, 202, 54, 28, "− m");
  pillButton(ID_MPLUS,  252, 202, 54, 28, "+ m");
}

// =====================================================================
//  TELA: MEDIDORES  (gauges com ponteiro "elástico" + sliders)
// =====================================================================
constexpr int ID_SLIDER0 = 50;
struct Gauge {
  const char* label; const char* unit; float maxV;
  float target, value, vel;
  bool  traffic;     // cor verde->amarelo->vermelho
};
Gauge GA[2] = {
  { "Velocidade", "km/h", 240, 80, 0, 0, true },
  { "Potência",   "%",    100, 45, 0, 0, false },
};
constexpr int G_CX[2] = { 82, 238 };
constexpr int G_CY = 116, G_R = 64, G_TH = 11;
constexpr int SL_Y = 206, SL_W = 128;

void updateGauge()
{
  for (int i = 0; i < 2; i++) {
    Gauge& g = GA[i];
    int sx = G_CX[i] - SL_W / 2;
    if (T.pressed && inRect(T.x, T.y, sx, SL_Y - 12, SL_W, 24, 6)) g_active = ID_SLIDER0 + i;
    if (g_active == ID_SLIDER0 + i && T.down)
      g.target = clampf((T.x - sx) / (float)SL_W, 0, 1) * g.maxV;
    // mola amortecida: o ponteiro "passa um pouco" e volta (efeito analógico)
    if (g_anim) {
      float k = 60, damp = 9;
      g.vel += ((g.target - g.value) * k - g.vel * damp) * g_dt;
      g.value += g.vel * g_dt;
    } else { g.value = g.target; g.vel = 0; }
  }
}

uint32_t gaugeColor(const Gauge& g, float t)
{
  if (!g.traffic) return mix(mix(COL_BG, accent(), 0.55f), accent(), t);
  return t < 0.5f ? mix(COL_GOOD, COL_WARN, t * 2) : mix(COL_WARN, COL_BAD, (t - 0.5f) * 2);
}

void drawGauge()
{
  char buf[24];
  for (int i = 0; i < 2; i++) {
    const Gauge& g = GA[i];
    int cx = G_CX[i], cy = G_CY;
    if (inBand(cy - G_R - 2, 2 * G_R + 4)) {
      const float A0 = 135, SWEEP = 270;
      float t = clampf(g.value / g.maxV, 0, 1);
      // trilho
      G.fillArc(cx, cy, G_R - G_TH, G_R, A0, A0 + SWEEP, C(COL_CARD2));
      // valor em segmentos com degradê de cor
      int segs = (int)(t * 54);
      for (int k = 0; k < segs; k++) {
        float a0 = A0 + k * SWEEP / 54, a1 = A0 + (k + 1) * SWEEP / 54 + 0.6f;
        G.fillArc(cx, cy, G_R - G_TH, G_R, a0, a1, C(gaugeColor(g, k / 53.0f)));
      }
      float aEnd = A0 + t * SWEEP;
      if (t > 0.002f) {
        G.fillArc(cx, cy, G_R - G_TH, G_R, A0 + segs * SWEEP / 54, aEnd, C(gaugeColor(g, t)));
        float ar = aEnd * PI_F / 180, rm = G_R - G_TH / 2.0f;
        G.fillSmoothCircle((int)(cx + cosf(ar) * rm), (int)(cy + sinf(ar) * rm), G_TH / 2 + 3, C(COL_WHITE));
        G.fillSmoothCircle((int)(cx + cosf(ar) * rm), (int)(cy + sinf(ar) * rm), G_TH / 2 - 1, C(gaugeColor(g, t)));
      }
      // marcas
      for (int k = 0; k <= 10; k++) {
        float a = (A0 + k * SWEEP / 10) * PI_F / 180;
        float r0 = G_R - G_TH - 5, r1 = G_R - G_TH - (k % 5 == 0 ? 12 : 8);
        line(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, k % 5 == 0 ? 1.0f : 0.6f,
             k % 5 == 0 ? COL_MUTED : COL_DIM);
      }
      snprintf(buf, sizeof(buf), "%d", (int)lroundf(fmaxf(g.value, 0)));
      text(buf, cx, cy - 4, &F30, COL_TEXT, textdatum_t::middle_center);
      text(g.unit, cx, cy + 20, &F12, COL_MUTED, textdatum_t::middle_center);
      text(g.label, cx, cy + G_R - 6, &F14, COL_TEXT, textdatum_t::middle_center);
    }
    // slider
    int sx = cx - SL_W / 2;
    if (inBand(SL_Y - 14, 28)) {
      float t = clampf(g.target / g.maxV, 0, 1);
      float p = g_press[ID_SLIDER0 + i];
      rrect(sx, SL_Y - 3, SL_W, 6, 3, COL_CARD2);
      rrect(sx, SL_Y - 3, (int)(6 + (SL_W - 6) * t), 6, 3, gaugeColor(g, t));
      int kx = sx + (int)(SL_W * t);
      if (p > 0.01f) circle(kx, SL_Y, (int)(10 + 6 * p), mix(COL_BG, gaugeColor(g, t), 0.3f * p));
      circle(kx, SL_Y, 9, COL_WHITE);
      circle(kx, SL_Y, 4, gaugeColor(g, t));
    }
  }
  text("Arraste os controles", W / 2, H - 10, &F12, COL_DIM, textdatum_t::middle_center);
}

// =====================================================================
//  TELA: AJUSTES
// =====================================================================
constexpr int ID_SWATCH0 = 60, ID_TOG_ANIM = 66, ID_TOG_FPS = 67, ID_BRIGHT = 68, ID_CALIB = 69;
float g_togAnim = 1, g_togFps = 0;
bool  g_calibRequest = false;

constexpr int SW_X0 = 150;
inline int swatchX(int i) { return SW_X0 + 12 + i * 27; }

void updateSettings()
{
  for (int i = 0; i < N_ACCENTS; i++)
    if (button(ID_SWATCH0 + i, swatchX(i) - 12, 44, 24, 24)) g_accent = i;
  if (button(ID_TOG_ANIM, 250, 82, 56, 30)) g_anim = !g_anim;
  if (button(ID_TOG_FPS,  250, 116, 56, 30)) g_showFps = !g_showFps;
  if (T.pressed && inRect(T.x, T.y, 110, 150, 190, 26, 4)) g_active = ID_BRIGHT;
  if (g_active == ID_BRIGHT && T.down) {
    g_brightness = clampf((T.x - 120) / 176.0f, 0.05f, 1) * 255;
    plat_set_brightness((uint8_t)g_brightness);
  }
  if (button(ID_CALIB, 16, 188, 288, 38)) g_calibRequest = true;
  approach(g_togAnim, g_anim ? 1 : 0, 14);
  approach(g_togFps, g_showFps ? 1 : 0, 14);
}

void toggle(int x, int y, float t, float p)
{
  uint32_t track = mix(COL_CARD2, accent(), t);
  rrect(x, y, 44, 24, 12, mix(COL_LINE, track, 0.9f));
  rrect(x + 1, y + 1, 42, 22, 11, track);
  int kx = (int)(x + 12 + t * 20);
  if (p > 0.01f) circle(kx, y + 12, (int)(11 + 5 * p), mix(COL_CARD, accent(), 0.25f * p));
  circle(kx, y + 12, 9, COL_WHITE);
}

void drawSettings()
{
  card(8, BAR_H + 6, 304, H - BAR_H - 12, 12, COL_CARD);
  // cor de destaque
  text("Cor de destaque", 20, 56, &F14, COL_TEXT);
  for (int i = 0; i < N_ACCENTS; i++) {
    int x = swatchX(i);
    float p = g_press[ID_SWATCH0 + i];
    if (i == g_accent) { circle(x, 56, 12, ACCENTS[i]); circle(x, 56, 10, COL_CARD); }
    circle(x, 56, (int)(7 + 2 * p), ACCENTS[i]);
  }
  G.drawFastHLine(20, 78, 280, C(COL_LINE));
  text("Animações suaves", 20, 97, &F14, COL_TEXT);
  toggle(256, 85, g_togAnim, g_press[ID_TOG_ANIM]);
  G.drawFastHLine(20, 114, 280, C(COL_LINE));
  text("Mostrar FPS", 20, 131, &F14, COL_TEXT);
  toggle(256, 119, g_togFps, g_press[ID_TOG_FPS]);
  G.drawFastHLine(20, 148, 280, C(COL_LINE));

  // brilho
  text("Brilho", 20, 164, &F14, plat_has_backlight() ? COL_TEXT : COL_DIM);
  if (!plat_has_backlight()) text("(LED no 3V3)", 20, 179, &F12, COL_DIM);
  float t = g_brightness / 255.0f;
  rrect(120, 161, 176, 6, 3, COL_CARD2);
  rrect(120, 161, (int)(6 + 170 * t), 6, 3, accent());
  int kx = 120 + (int)(176 * t);
  float p = g_press[ID_BRIGHT];
  if (p > 0.01f) circle(kx, 164, (int)(10 + 6 * p), mix(COL_CARD, accent(), 0.3f * p));
  circle(kx, 164, 8, COL_WHITE);

  // botão de calibração
  float bp = g_press[ID_CALIB];
  card(16, 190, 288, 36, 10, mix(COL_CARD, accent(), 0.10f + 0.25f * bp), mix(COL_LINE, accent(), 0.5f + 0.5f * bp));
  int ix = 108, iy = 208;
  if (inBand(iy - 8, 16)) {
    G.drawCircle(ix, iy, 6, C(accent()));
    line(ix - 9, iy, ix + 9, iy, 0.6f, accent());
    line(ix, iy - 9, ix, iy + 9, 0.6f, accent());
  }
  text("Calibrar touch", 124, 208, &F14, COL_TEXT);
}

// =====================================================================
//  TELA: SISTEMA
// =====================================================================
void drawSystem()
{
  char buf[40];
  card(8, BAR_H + 6, 304, H - BAR_H - 12, 12, COL_CARD);
  struct Row { const char* k; char v[28]; } rows[7];
  uint32_t up = g_now / 1000;
  snprintf(rows[0].v, 28, "%s", plat_chip_name());                         rows[0].k = "Chip";
  snprintf(rows[1].v, 28, "%u MHz", (unsigned)plat_cpu_mhz());            rows[1].k = "CPU";
  snprintf(rows[2].v, 28, "%u KB", (unsigned)(plat_free_heap() / 1024));  rows[2].k = "RAM livre";
  snprintf(rows[3].v, 28, "%u KB", (unsigned)(plat_min_free_heap() / 1024)); rows[3].k = "RAM mínima";
  snprintf(rows[4].v, 28, "%uh %02um %02us", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60)); rows[4].k = "Ligado há";
  snprintf(rows[5].v, 28, "%dx%d", W, H);                                 rows[5].k = "Tela";
  if (T.down) snprintf(rows[6].v, 28, "%d, %d", T.x, T.y); else snprintf(rows[6].v, 28, "—");
  rows[6].k = "Toque (x,y)";
  for (int i = 0; i < 7; i++) {
    int y = BAR_H + 22 + i * 27;
    text(rows[i].k, 20, y, &F12, COL_MUTED);
    bool longV = textW(rows[i].v, &F14) > 100;
    text(rows[i].v, 170, y, longV ? &F12 : &F14, COL_TEXT, textdatum_t::middle_right);
    if (i < 6) G.drawFastHLine(20, y + 13, 150, C(COL_LINE));
  }

  // mini gráfico de FPS
  int gx = 186, gy = BAR_H + 14, gw = 116, gh = 110;
  card(gx, gy, gw, gh, 10, COL_CARD2);
  text("Quadros/s", gx + 10, gy + 14, &F12, COL_MUTED);
  snprintf(buf, sizeof(buf), "%.1f", g_fps);
  text(buf, gx + 10, gy + 38, &F20, COL_TEXT);
  int n = sizeof(g_fpsHist) / sizeof(g_fpsHist[0]);
  float mx = 10;
  for (float f : g_fpsHist) mx = fmaxf(mx, f);
  for (int i = 0; i < n; i++) {
    int bh = (int)(g_fpsHist[i] / mx * 48);
    if (bh < 1) bh = 1;
    G.fillRect(gx + 10 + i * 2, gy + gh - 10 - bh, 1, bh, C(mix(COL_CARD2, accent(), 0.4f + 0.6f * i / n)));
  }
  // marca de toque: desenha um alvo onde o dedo está (teste de precisão)
  text("Toque em qualquer", gx + gw / 2, gy + gh + 22, &F12, COL_DIM, textdatum_t::middle_center);
  text("lugar p/ testar", gx + gw / 2, gy + gh + 38, &F12, COL_DIM, textdatum_t::middle_center);
  if (T.down && T.y > BAR_H) {
    G.drawCircle(T.x, T.y, 14, C(accent()));
    line(T.x - 20, T.y, T.x + 20, T.y, 0.5f, accent());
    line(T.x, T.y - 20, T.x, T.y + 20, 0.5f, accent());
    circle(T.x, T.y, 3, accent());
  }
}

// ---------------------------------------------------------------------
//  Desenho do quadro inteiro (chamado uma vez por faixa)
// ---------------------------------------------------------------------
void drawScene()
{
  G.fillRect(0, g_bandY, W, BAND_H, C(COL_BG));
  drawStatusBar();
  switch (g_screen) {
    case SCR_HOME:     drawHome(); break;
    case SCR_DASH:     drawDash(); break;
    case SCR_CHART:    drawChart(); break;
    case SCR_CLOCK:    drawClock(); break;
    case SCR_GAUGE:    drawGauge(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_SYSTEM:   drawSystem(); break;
  }
}

void render()
{
  g_out->startWrite();
  for (int band = 0; band < H / BAND_H; band++) {
    g_bandY = band * BAND_H;
    // truque: o sprite "acha" que tem 240 linhas, mas o ponteiro é deslocado
    // para que a faixa atual caia exatamente dentro do buffer de 120 linhas.
    // O clip garante que nada é escrito fora da faixa.
    G.setBuffer(g_buf - (size_t)g_bandY * W, W, H, 16);
    unclip();
    drawScene();
    g_view.pushSprite(g_out, 0, g_bandY);
  }
  g_out->endWrite();
}

int32_t parseCompileTime()
{
  const char* t = __TIME__;   // "hh:mm:ss"
  return ((t[0] - '0') * 10 + (t[1] - '0')) * 3600 + ((t[3] - '0') * 10 + (t[4] - '0')) * 60 +
         ((t[6] - '0') * 10 + (t[7] - '0'));
}

bool loadFont(lgfx::VLWfont& f, lgfx::PointerWrapper& w, const uint8_t* data, size_t len)
{
  w.set(data, len);
  return f.loadFont(&w);
}

} // namespace

// =====================================================================
//  API pública
// =====================================================================
bool ui_begin(lgfx::LovyanGFX* out)
{
  g_out = out;
  g_buf = (uint16_t*)malloc((size_t)W * BAND_H * 2);
  if (!g_buf) return false;
  g_view.setBuffer(g_buf, W, BAND_H, 16);

  loadFont(F12, W12, inter_12_vlw, inter_12_vlw_len);
  loadFont(F14, W14, inter_14_vlw, inter_14_vlw_len);
  loadFont(F20, W20, inter_20_vlw, inter_20_vlw_len);
  loadFont(F30, W30, inter_30_vlw, inter_30_vlw_len);
  loadFont(F52, W52, inter_52_vlw, inter_52_vlw_len);

  g_now = g_lastFrame = plat_millis();
  g_rng ^= g_now * 2654435761u;
  g_timeBase = parseCompileTime() - (int32_t)(g_now / 1000);
  initSeries();
  g_lastSample = g_now;
  for (auto& g : GA) g.value = 0;
  plat_set_brightness((uint8_t)g_brightness);
  g_fade = 0;
  return true;
}

void ui_loop()
{
  uint32_t now = plat_millis();
  g_dt = clampf((now - g_lastFrame) / 1000.0f, 0.001f, 0.1f);
  if (now != g_lastFrame) g_fps = lerpf(g_fps, 1.0f / g_dt, 0.1f);
  g_lastFrame = g_now = now;
  if (now - g_fpsLastPush > 250) {
    g_fpsLastPush = now;
    int n = sizeof(g_fpsHist) / sizeof(g_fpsHist[0]);
    memmove(g_fpsHist, g_fpsHist + 1, (n - 1) * sizeof(float));
    g_fpsHist[n - 1] = g_fps;
  }

  readTouch();

  while (now - g_lastSample >= SAMPLE_MS) { g_lastSample += SAMPLE_MS; sampleSeries(); }
  g_sampleFrac = g_anim ? clampf((now - g_lastSample) / (float)SAMPLE_MS, 0, 1) : 0;
  for (auto& s : S) approach(s.shown, s.v, 6);

  updateStatusBar();
  switch (g_screen) {
    case SCR_HOME:     updateHome(); break;
    case SCR_DASH:     updateDash(); break;
    case SCR_CHART:    updateChart(); break;
    case SCR_CLOCK:    updateClock(); break;
    case SCR_GAUGE:    updateGauge(); break;
    case SCR_SETTINGS: updateSettings(); break;
    case SCR_SYSTEM:   break;
  }
  // a tela de gráficos precisa da escala mesmo quando volta do dashboard
  if (g_screen != SCR_CHART) g_axInit = false;

  for (int i = 0; i < N_IDS; i++) approach(g_press[i], (g_active == i && T.down) ? 1.0f : 0.0f, 16);
  approach(g_fade, 1, 9);

  render();

  if (T.released) g_active = -1;

  if (g_calibRequest) {
    g_calibRequest = false;
    plat_calibrate_touch();
    g_active = -1;
    T = TouchState();
  }
}
