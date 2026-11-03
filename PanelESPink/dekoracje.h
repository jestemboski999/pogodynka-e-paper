#pragma once
#include "dekoracje_bitmapy.h"
#ifndef BIRTHDAY_MONTH
#define BIRTHDAY_MONTH 0
#define BIRTHDAY_DAY 0
#endif
// Swiateczne oprawy: rama (girlandy przez gore ekranu, ozdoby w marginesach),
// motywy rozsiane w wolnych miejscach i zyczenia w drugim wierszu podsumowania.
enum HolidayDecoration { DEC_NONE, DEC_CHRISTMAS, DEC_EASTER, DEC_NEWYEAR, DEC_HEARTS, DEC_DONUT, DEC_FLAGS,
                         DEC_SANTA, DEC_SPRING, DEC_CHILDREN, DEC_CANDLE, DEC_BIRTHDAY };
static int easterYday(int year){
 int a=year%19,b=year/100,c=year%100,d=b/4,e=b%4,f=(b+8)/25,g=(b-f+1)/3;
 int h=(19*a+b-d-g+15)%30,i=c/4,k=c%4,l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451;
 tm t={};t.tm_year=year-1900;t.tm_mon=(h+l-7*m+114)/31-1;t.tm_mday=(h+l-7*m+114)%31+1;t.tm_hour=12;t.tm_isdst=-1;mktime(&t);return t.tm_yday;
}
static HolidayDecoration holidayDecoration(const tm &t){
 int mon=t.tm_mon+1,day=t.tm_mday;
 if(BIRTHDAY_MONTH&&mon==BIRTHDAY_MONTH&&day==BIRTHDAY_DAY)return DEC_BIRTHDAY;
 if(mon==12&&day>=24&&day<=26)return DEC_CHRISTMAS;
 if((mon==12&&day==31)||(mon==1&&day==1))return DEC_NEWYEAR;
 int easter=easterYday(t.tm_year+1900);
 if(t.tm_yday==easter||t.tm_yday==easter+1)return DEC_EASTER;
 if(t.tm_yday==easter-52)return DEC_DONUT;                       // tlusty czwartek
 if(mon==2&&day==14)return DEC_HEARTS;
 if(mon==3&&day==21)return DEC_SPRING;
 if((mon==5&&(day==2||day==3))||(mon==11&&day==11))return DEC_FLAGS;
 if(mon==6&&day==1)return DEC_CHILDREN;
 if(mon==11&&(day==1||day==2))return DEC_CANDLE;
 if(mon==12&&day==6)return DEC_SANTA;
 return DEC_NONE;
}
// wet/snow: czy dzis w prognozie sa opady / snieg -- kilka zyczen reaguje na pogode.
static const char *holidayGreeting(const tm &t,bool wet,bool snow){
 int mon=t.tm_mon+1,day=t.tm_mday;
 switch(holidayDecoration(t)){
 case DEC_BIRTHDAY:return "Wszystkiego najlepszego!";
 case DEC_CHRISTMAS:return snow?"Wesołych, białych Świąt!":"Wesołych Świąt!";
 case DEC_NEWYEAR:return mon==12?"Do siego roku!":"Szczęśliwego Nowego Roku!";
 case DEC_EASTER:
  if(t.tm_yday==easterYday(t.tm_year+1900))return "Wesołego Alleluja!";
  return wet?"Śmigus-dyngus, tym razem także z nieba":"Mokrego dyngusa!";
 case DEC_DONUT:return "Smacznego pączka!";
 case DEC_HEARTS:return "Szczęśliwych Walentynek!";
 case DEC_SPRING:return "Pierwszy dzień wiosny!";
 case DEC_FLAGS:return mon==11?"Święto Niepodległości":day==3?"Święto Konstytucji 3 Maja":"Dzień Flagi";
 case DEC_CHILDREN:return "Wesołego Dnia Dziecka!";
 case DEC_SANTA:return "Mikołajki! Zajrzyj pod poduszkę";
 default:return "";   // 1-2 listopada: same znicze, bez zyczen
 }
}
static const DecoTheme *decoTheme(HolidayDecoration kind){
 switch(kind){
 case DEC_CHRISTMAS:return &deco_christmas;case DEC_EASTER:return &deco_easter;
 case DEC_NEWYEAR:return &deco_newyear;case DEC_HEARTS:return &deco_hearts;
 case DEC_DONUT:return &deco_donut;case DEC_FLAGS:return &deco_flags;
 case DEC_SANTA:return &deco_santa;case DEC_SPRING:return &deco_spring;
 case DEC_CHILDREN:return &deco_children;case DEC_CANDLE:return &deco_candle;
 case DEC_BIRTHDAY:return &deco_birthday;default:return nullptr;
 }
}
// Czy nazwa miejscowosci zostaje (w wiekszosc swiat zastepuje ja girlanda).
static bool decoShowsLabel(const tm &t){const DecoTheme *th=decoTheme(holidayDecoration(t));return !th||th->showLabel;}

static bool fbInk(int x,int y){return fb.getBuffer()[y*(DISP_WIDTH/8)+(x>>3)]&(0x80>>(x&7));}
// Rama: RLE (0..127 = n+1 bajtow doslownie, 128..255 = n-127 bajtow zerowych), wiersze po 100 B.
static void drawDecoFrame(const DecoTheme &th){
 uint32_t pos=0;
 for(uint16_t i=0;i<th.frameLen;){
  uint8_t c=th.frame[i++];
  if(c>=128){pos+=c-127;continue;}
  for(int n=0;n<=c;n++,pos++){
   uint8_t v=th.frame[i++];if(!v)continue;
   int y=pos/(DISP_WIDTH/8),x=(pos%(DISP_WIDTH/8))*8;
   for(int b=0;b<8;b++)if(v&(0x80>>b))fb.drawPixel(x+b,y,INK);
  }
 }
}
// Miejsce na motyw: w calosci na ekranie, poza strefami odswiezanymi osobno
// (zegar, godzina aktualizacji, bateria) i marginesami ramy, z odstepem od tuszu.
// Gorna czesc (zegar, ikona, temperatura) zostaje czysta: tam motyw mylilby sie z danymi.
static bool decoSpotFree(int x,int y,int w,int h,int m){
 if(x-m<30||y-m<205||x+w+m>770||y+h+m>478)return false;
 if(x+w+m>712&&y+h+m>454)return false;               // bateria
 for(int yy=y-m;yy<y+h+m;yy++)for(int xx=x-m;xx<x+w+m;xx++)if(fbInk(xx,yy))return false;
 return true;
}
// Rozsiewa motywy po wolnych miejscach. Ziarno z daty: ten sam dzien i ta sama
// tresc daja identyczny obraz (odcisk ekranu nie wymusza zbednych odswiezen).
static void scatterDecoMotifs(const DecoTheme &th,const tm &t){
 if(!th.motifCount||!th.maxScatter)return;
 uint32_t seed=(uint32_t)(t.tm_year*400+t.tm_yday)*2654435761u+12345u;
 auto next=[&seed](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
 const int cols=7,rows=4,cellW=740/cols,cellH=(478-205)/rows;
 uint8_t order[cols*rows];for(int i=0;i<cols*rows;i++)order[i]=i;
 for(int i=cols*rows-1;i>0;i--){int j=next()%(i+1);uint8_t tmp=order[i];order[i]=order[j];order[j]=tmp;}
 int placed=0;
 for(int k=0;k<cols*rows&&placed<th.maxScatter;k++){
  int cx=30+(order[k]%cols)*cellW,cy=205+(order[k]/cols)*cellH;
  for(int attempt=0;attempt<6;attempt++){
   const DecoMotif &m=th.motifs[next()%th.motifCount];
   int x=cx+next()%max(1,cellW-m.w),y=cy+next()%max(1,cellH-m.h);
   if(!decoSpotFree(x,y,m.w,m.h,13))continue;
   fb.drawBitmap(x,y,m.bits,m.w,m.h,INK);placed++;break;
  }
 }
}
// Wywolywane na koncu skladania ekranu, gdy cala tresc jest juz narysowana.
static void drawHolidayDecoration(const tm &t){
 HolidayDecoration kind=holidayDecoration(t);
 const DecoTheme *th=decoTheme(kind);if(!th)return;
 drawDecoFrame(*th);
 if(kind==DEC_NEWYEAR){
  // Numer zaczynajacego sie roku na tle serpentyn (31.12 -- juz nastepny).
  char year[8];snprintf(year,sizeof(year),"%d",t.tm_year+1900+(t.tm_mon==11?1:0));
  int w=getStringWidth(year,SUMMARY);
  fb.fillRoundRect(190-w/2-10,2,w+20,40,8,PAPER);
  drawString(190,36,year,SUMMARY,CENTER);
 }
 scatterDecoMotifs(*th,t);
}
