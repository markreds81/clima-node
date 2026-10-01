#pragma once

#include <stdint.h>

// Simboli presenti nella ROM dell'HD44780 (set di caratteri A00)
#define LCD_DEGREE        ((char)223)  // °
#define LCD_RIGHT_ARROW   ((char)126)  // →
#define LCD_MIDDLE_DOT    ((char)165)  // ·
#define LCD_FULL_BLOCK    ((char)255)  // █

// Caratteri personalizzati dell'LCD (slot CGRAM 0-7)
#define ICON_THERMOMETER  0
#define ICON_DROP         1
#define ICON_TREND_UP     2
#define ICON_TREND_DOWN   3

static uint8_t thermometerGlyph[8] = {
  0b00100,
  0b01010,
  0b01010,
  0b01110,
  0b01110,
  0b11111,
  0b11111,
  0b01110
};

static uint8_t dropGlyph[8] = {
  0b00100,
  0b00100,
  0b01110,
  0b01110,
  0b11111,
  0b11111,
  0b01110,
  0b00000
};

static uint8_t trendUpGlyph[8] = {
  0b00100,
  0b01110,
  0b10101,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00000
};

static uint8_t trendDownGlyph[8] = {
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b10101,
  0b01110,
  0b00100,
  0b00000
};
