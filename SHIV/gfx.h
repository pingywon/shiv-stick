// SHIV - graphics shim: M5Unified on the device, bare M5GFX sprite on the PC harness.
#pragma once
#ifdef SHIV_HOST
  #define LGFX_USE_V1
  #include <lgfx/v1/LGFXBase.hpp>
  #include <lgfx/v1/LGFX_Sprite.hpp>
  #include <lgfx/v1/lgfx_fonts.hpp>
#else
  #include <M5Unified.h>
#endif

namespace shiv {
typedef lgfx::v1::LGFX_Sprite Canvas;
typedef lgfx::v1::textdatum_t Datum;
typedef uint32_t Col;                 // always RGB888 (0xRRGGBB) - lgfx converts by type
namespace FN = lgfx::v1::fonts;

inline Col mix(Col a, Col b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return ((Col)r << 16) | ((Col)g << 8) | (Col)bl;
}
}  // namespace shiv
