// Kernlogica van de woordklok: geen WLED- of Arduino-afhankelijkheden, dus ook op een pc te testen.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <vector>

namespace wordclock {

struct Data {
  std::vector<uint16_t> wStart;   // eerste LED van elk woord
  std::vector<uint8_t>  wLen;     // aantal LEDs van elk woord
  std::vector<uint8_t>  masks;    // 288 tijdstippen x bpm bytes
  uint16_t marks[5] = {0,0,0,0,0};// LEDs van "+", "1", "2", "3", "4"
  bool    hasMarks = false;
  uint8_t bpm = 0;                // bytes per masker = ceil(W/8)

  size_t words() const { return wStart.size(); }
  bool valid() const { return bpm > 0 && masks.size() >= 288u * bpm; }
};

inline int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;                      // '=' en witruimte worden overgeslagen
}

// Decodeert base64 naar out (max. cap bytes). Geeft het aantal gedecodeerde bytes terug.
inline size_t b64decode(const char* s, uint8_t* out, size_t cap) {
  size_t o = 0; uint32_t acc = 0; int bits = 0;
  for (; *s; ++s) {
    int v = b64val(*s);
    if (v < 0) continue;
    acc = (acc << 6) | (uint32_t)v; bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o < cap) out[o] = (uint8_t)((acc >> bits) & 0xFF);
      ++o;
      acc &= (1u << bits) - 1;
    }
  }
  return o;
}

// Zet in lit[] (1 = aan) welke LEDs bij uur h en minuut m horen. offset = eerste LED van de matrix.
inline void buildLit(const Data& d, int h, int m, int offset, std::vector<uint8_t>& lit) {
  std::fill(lit.begin(), lit.end(), 0);
  if (!d.valid()) return;
  const size_t n = lit.size();
  auto set = [&](long p) { p += offset; if (p >= 0 && (size_t)p < n) lit[(size_t)p] = 1; };
  int slot = (h * 60 + m) / 5;                    // 0..287
  const uint8_t* mk = &d.masks[(size_t)slot * d.bpm];
  for (size_t i = 0; i < d.words(); ++i)
    if (mk[i >> 3] & (1 << (i & 7)))
      for (uint16_t k = 0; k < d.wLen[i]; ++k) set((long)d.wStart[i] + k);
  int k = m % 5;                                  // minuuttekens: "+" en cijfer k
  if (k > 0 && d.hasMarks) { set(d.marks[0]); set(d.marks[k]); }
}

} // namespace wordclock
