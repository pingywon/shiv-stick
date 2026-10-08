#define LGFX_USE_V1
#include <lgfx/v1/LGFXBase.hpp>
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <cstdio>
using lgfx::v1::LGFX_Sprite;
struct F{const char*n; const lgfx::v1::IFont* f;};
#define X(a) {#a,&fonts::a}
F fl[]={X(Font2),X(Font4),X(FreeMonoBold9pt7b),X(FreeMonoBold12pt7b),X(FreeMonoBold18pt7b),X(FreeSansBold9pt7b),X(FreeSansBold12pt7b),X(FreeSansBold18pt7b),X(FreeSansBold24pt7b),X(Orbitron_Light_24),X(Orbitron_Light_32),X(DejaVu18),X(DejaVu24),X(DejaVu40),X(DejaVu56),X(Font6),X(Font7),X(Font8)};
int main(){
  int n=sizeof(fl)/sizeof(fl[0]); int W=480,H=0; 
  LGFX_Sprite m; m.setColorDepth(16); m.createSprite(200,120);
  LGFX_Sprite c; c.setColorDepth(16); c.createSprite(W,n*80);
  c.fillScreen(0); int y=0;
  for(int i=0;i<n;i++){
    // measure cap height of 'H' (or '8' for digit fonts)
    m.fillScreen(0); m.setFont(fl[i].f); m.setTextColor(0xFFFF); m.setCursor(10,10); m.print("8");
    int top=999,bot=-1,l=999,r=-1; for(int yy=0;yy<120;yy++)for(int xx=0;xx<200;xx++) if(m.readPixelValue(xx,yy)){ if(yy<top)top=yy; if(yy>bot)bot=yy; if(xx<l)l=xx; if(xx>r)r=xx; }
    int cap=bot-top+1; c.setFont(fl[i].f); int adv=c.textWidth("8"); int wM=c.textWidth("ABCDEFGHIJ")/10; int fh=c.fontHeight();
    printf("%-22s digit_h=%2d digit_adv=%2d avgUpperW=%2d lineH=%2d  upper_chars_per_240=%d\n",fl[i].n,cap,adv,wM,fh, wM?240/wM:0);
    c.setTextColor(0xFFFF); c.setCursor(4,y+4); c.print("PI-HOLE 38% 13:50"); 
    c.drawFastVLine(240,y,fh+8,0x1F00); y+=fh+10;
  }
  FILE* f=fopen("build/spec.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",W,y);
  for(int yy=0;yy<y;yy++)for(int x=0;x<W;x++){ uint16_t p=c.readPixelValue(x,yy); uint16_t v=(p>>8)|(p<<8);
    fputc(((v>>11)&31)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);} fclose(f); return 0; }
