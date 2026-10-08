#define LGFX_USE_V1
#include <lgfx/v1/LGFXBase.hpp>
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <cstdio>
using lgfx::v1::LGFX_Sprite; using lgfx::v1::textdatum_t;
int main(){
  LGFX_Sprite c; c.setColorDepth(16); c.createSprite(240,135);
  c.fillScreen(0x0000);
  c.setTextColor(0x07FF); c.setFont(&fonts::Font7); c.setTextDatum(textdatum_t::middle_center);
  c.drawString("13:50", 120, 60);
  c.setFont(&fonts::FreeMonoBold9pt7b); c.setTextColor(0xF81F); c.drawString("SHIV // ONLINE", 120, 118);
  FILE* f=fopen("build/poc.ppm","wb"); fprintf(f,"P6\n240 135\n255\n");
  for(int y=0;y<135;y++)for(int x=0;x<240;x++){ auto p=c.readPixelValue(x,y); uint16_t v=(p>>8)|(p<<8); v=p;
    unsigned char r=((v>>11)&31)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31; fputc(r,f);fputc(g,f);fputc(b,f);} fclose(f); return 0; }
