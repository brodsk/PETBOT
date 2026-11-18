#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <pgmspace.h>

// 18 monochrome expressions digitized from the user's reference sheet.
// Each expression is 56x24 RLE; rendered 2x on the 128x64 OLED.
static const char faceRleB64[] PROGMEM =
"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A///Fwz/BwEYASEC/xgCIQH/BAEZASEBMgH/BgEZAiAC/xoH////////////AQ8lEv//////BgH/LgH/CAEuAf8IARUM/xUBIAH/FgEfAf8XCP///////////////wMB/wETJRL/AQIIBRICJAIrBjYB/wIBBwcSASUCKwY1Av8CAwcHEQImAisGNAL/BAoPAycKMgP/BgoWARwBIgEpCv8WARwBIgH/FwEbAyIB/xgDHQIgAv8YAR4D/xgBHwH/GAIeAv8ZBv///////////ywE/wEDKQr/AgQnBC8F/wUEJgMuB/8HBSUCLQn/CgQtCv8MBSwL/w0ELAv/CQYsC/8FBi0J/wMEGQgtCf8YBh8CLgf/GQEgATAD/xkBHwL/GQEfAv8ZAh8C/xoBHwL/GgIdA/8bBP////8EDiYO/wgBDQEpAi8C/wgBDQH/CAENASoB/wgBDQEqAf8IAQ0BKgH/CAENASoB/wgBDQEVARsBIAEqAf8IAQ0BFgEaAiABKgH/CAENARYBGgIfASoB/w0BFwMcA////////////////wsCKgf/CAcoAzAC/////wQJLQf/AwsqC/8CBAwCKQQzA/8BBA0CKQErATQC/wEEDQIkASkDNAP/AQIEAQ0CFAEYARwBIAEkASoCNAP/AQIEAg0CEwEXARsBHwIjASoCNAP/AQMFAQ0CEwEWAhsBHwEiAioDNAP/AQQGAQ0CFgEqBDMD/wIBBAIMAysEMgT/AgwsCf8EByEBLQf/Fg3/FgEiAf8WASIB/xYBIgL/Fg3//////w==";
static const uint16_t faceRleOffsets[18] PROGMEM = {0,120,312,462,610,792,976,1186,1268,1344,1476,1588,1746,1840,1890,2006,2114,2230};
static uint8_t faceRleDecoded[2398];
static bool faceRleReady = false;

static int8_t faceB64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
static void decodeFaceRle() {
  if (faceRleReady) return;
  uint32_t out = 0;
  uint32_t acc = 0;
  uint8_t bits = 0;
  for (uint16_t i = 0; ; ++i) {
    char c = (char)pgm_read_byte(faceRleB64 + i);
    if (!c || c == '=') break;
    int8_t v = faceB64Value(c);
    if (v < 0) continue;
    acc = (acc << 6) | (uint8_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out < sizeof(faceRleDecoded)) faceRleDecoded[out++] = (uint8_t)(acc >> bits);
    }
  }
  faceRleReady = (out == sizeof(faceRleDecoded));
}
static void drawFaceExpression(Adafruit_GFX& gfx, uint8_t index, int16_t x, int16_t y) {
  decodeFaceRle();
  if (!faceRleReady) return;
  uint16_t p = pgm_read_word(faceRleOffsets + (index % 18));
  for (uint8_t row = 0; row < 24; ++row) {
    while (true) {
      uint8_t start = faceRleDecoded[p++];
      if (start == 255) break;
      uint8_t len = faceRleDecoded[p++];
      gfx.fillRect(x + start * 2, y + row * 2, len * 2, 2, 1);
    }
  }
}
