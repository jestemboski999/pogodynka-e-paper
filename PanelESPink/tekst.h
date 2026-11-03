#pragma once
#include "czcionki.h"
enum Align { LEFT,CENTER,RIGHT };
static uint16_t textColor = INK;
static int cursorX = 0;   // jak kursor Adafruit_GFX: gdzie skonczyl sie ostatni napis

static uint16_t utf8Next(const char *&p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *p) return ((c & 0x1F) << 6) | ((uint8_t)*p++ & 0x3F);
  if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
    uint16_t v = ((c & 0x0F) << 12) | (((uint8_t)p[0] & 0x3F) << 6) | ((uint8_t)p[1] & 0x3F);
    p += 2;
    return v;
  }
  return '?';
}

static const GlyphK *findGlyph(const FontK &f, uint16_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (f.g[m].cp == cp) return &f.g[m];
    if (f.g[m].cp < cp) lo = m + 1; else hi = m - 1;
  }
  Serial.printf("[tekst] brak glifu U+%04X\n", cp);
  return nullptr;
}

// tab = cyfry tabelaryczne: kazda cyfra zajmuje szerokosc najszerszej, wiec
// liczby w kolumnach i na skalach stoja rowno. W zwyklym tekscie i w liczbach
// stojacych samotnie zostaja cyfry proporcjonalne -- waska jedynka nie robi dziur.
static int glyphAdvance(const FontK &f, const GlyphK *g, bool tab) {
  return (tab && g->cp >= '0' && g->cp <= '9') ? f.digitAdv : g->adv;
}

static int getStringWidth(const char *s, const FontK &f, bool tab = false) {
  int w = 0;
  for (const char *p = s; *p;) {
    const GlyphK *g = findGlyph(f, utf8Next(p));
    if (g) w += glyphAdvance(f, g, tab);
  }
  return w;
}

static void drawGlyph(int x, int y, const FontK &f, const GlyphK *g) {
  const uint8_t *b = f.bmp + g->off;
  const int stride = (g->w + 7) / 8;
  for (int yy = 0; yy < g->h; yy++)
    for (int xx = 0; xx < g->w; xx++)
      if (b[yy * stride + (xx >> 3)] & (0x80 >> (xx & 7)))
        fb.drawPixel(x + g->x0 + xx, y + g->y0 + yy, textColor);
}

// (x, y) to punkt na linii bazowej
static void drawString(int x, int y, const char *s, const FontK &f, Align a = LEFT, bool tab = false) {
  int w = getStringWidth(s, f, tab);
  if (a == CENTER) x -= w / 2;
  else if (a == RIGHT) x -= w;
  for (const char *p = s; *p;) {
    const GlyphK *g = findGlyph(f, utf8Next(p));
    if (!g) continue;
    int adv = glyphAdvance(f, g, tab);
    drawGlyph(x + (adv - g->adv) / 2, y, f, g);
    x += adv;
  }
  cursorX = x;
}
