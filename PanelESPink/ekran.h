#pragma once
#include "podsumowanie.h"
#include "tekst.h"
#include "ikony.h"
#include "dekoracje.h"
#include "bateria.h"
#include "nowcast.h"
static void number(char *out,size_t n,float v,bool degree=true){
 if(!isfinite(v)){snprintf(out,n,"—");return;} int a=(int)lroundf(v);
 snprintf(out,n,"%s%d%s",a<0?"−":"",abs(a),degree?"°":"");
}
static void drawClock(const tm &t){
 fb.fillRect(16,50,334,112,PAPER);char b[16];snprintf(b,sizeof(b),"%02d:%02d",t.tm_hour,t.tm_min);drawString(25,151,b,CLOCK);
}
static const char *description(int c){
 if(c==0)return "Bezchmurnie";if(c==1||c==2)return "Przejaśnienia";if(c==3)return "Pochmurno";
 if(c==48)return "Mgła i szadź";if(c==45)return "Mgła";if(freezeCode(c))return "Marznące opady";
 if(hailCode(c))return "Burze z gradem";if(c>=95)return "Burze";if(snowCode(c))return "Śnieg";if(c>=51&&c<=57)return "Mżawka";
 if(wetCode(c))return "Deszcz";return "Brak danych";
}
static const uint8_t *weatherIcon(int c,bool day){
 if(c==0)return day?icon_sun:icon_sun_night;
 if(c==1||c==2)return day?icon_partly:icon_partly_night;
 if(snowCode(c))return icon_snow;if(c>=95)return icon_storm;
 if(wetCode(c))return icon_rain;if(c==45||c==48)return icon_fog;return icon_overcast;
}
#define LOCATION_LABEL "Warszawa · Wola"
static bool decorationsEnabled=true;   // wylacznik dla testow porownawczych
static bool batteryLow(){return batteryPercent>=0&&batteryPercent<=5;}
// Position on the 12-hour axis, using local midnight (also across DST).
static int midnightGraphX(time_t start){
 tm midnight;localtime_r(&start,&midnight);
 midnight.tm_hour=0;midnight.tm_min=0;midnight.tm_sec=0;midnight.tm_isdst=-1;
 time_t at=mktime(&midnight);
 if(at<start){midnight.tm_mday++;midnight.tm_isdst=-1;at=mktime(&midnight);}
 if(at<start||at>start+12*3600)return -1;
 return 80+(int)lround((double)(at-start)*640/(12*3600));
}
static void screenRainBlockText(const RainBlock &block,int index,time_t now,char *out,size_t size){
 rainBlockText(block,out,size);
 char signal[180];bool freezing;
 // The entire 2-hour near-term forecast fits inside the first 3-hour block.
 // Do not combine its rate with an unrelated model's probability or sum.
 if(index==0&&!rainBlockBlack(block)&&nearRainMessage(now,signal,sizeof(signal),freezing))snprintf(out,size,"Opady");
}
static void drawBattery(){
 bool low=batteryLow();int x=low?704:728,y=low?12:466,w=low?27:18,h=low?16:9;
 uint16_t ink=low?PAPER:INK,paper=low?INK:PAPER;
 if(low){fb.fillRoundRect(536,3,248,36,5,INK);textColor=PAPER;drawString(546,27,"Naładuj baterię",RAIN_S);}
 fb.fillRect(x,y,w,h,ink);fb.fillRect(x+1,y+1,w-2,h-2,paper);fb.fillRect(x+w,y+h/3,2,h/3,ink);
 if(batteryPercent>=0){int fill=(int)lroundf((w-4)*batteryPercent/100.f);if(fill)fb.fillRect(x+2,y+2,fill,h-4,ink);}
 char label[12];if(batteryPercent>=0)snprintf(label,sizeof(label),"%d%%",batteryPercent);else snprintf(label,sizeof(label),"—%%");
 drawString(low?774:788,low?27:475,label,low?RAIN_S:SMALL,RIGHT);textColor=INK;
}
static void composeScreen(const tm &ti,time_t now){
 fb.fillScreen(PAPER);textColor=INK;
 char b[180],v[32];tm ft={};if(wx.fetched)localtime_r(&wx.fetched,&ft);
 if(wx.fetched)snprintf(b,sizeof(b),"%s %02d:%02d",stale(now)?"Ostatnie dane":"Aktualizacja",ft.tm_hour,ft.tm_min);
 else snprintf(b,sizeof(b),"Oczekiwanie na dane");if(!batteryLow())drawString(768,25,b,SMALL,RIGHT);drawBattery();
 drawClock(ti);fb.fillRect(362,59,2,126,INK);
 static const char *days[]={"Niedziela","Poniedziałek","Wtorek","Środa","Czwartek","Piątek","Sobota"};
 static const char *shortDays[]={"Niedz.","Pon.","Wt.","Śr.","Czw.","Pt.","Sob."};
 static const char *months[]={"stycznia","lutego","marca","kwietnia","maja","czerwca","lipca","sierpnia","września","października","listopada","grudnia"};
 snprintf(b,sizeof(b),"%s, %d %s",days[ti.tm_wday],ti.tm_mday,months[ti.tm_mon]);
 if(getStringWidth(b,DATE)>315)snprintf(b,sizeof(b),"%s, %d %s",shortDays[ti.tm_wday],ti.tm_mday,months[ti.tm_mon]);
 drawString(32,188,b,DATE);
 bool fresh=!stale(now);int shownCode=displayCode(now);
 constexpr int weatherCenterX=465;
 if(fresh&&shownCode>=0&&!(wx.isDay<0&&(shownCode==0||shownCode==1||shownCode==2)))fb.drawBitmap(weatherCenterX-WEATHER_ICON_W/2,44,weatherIcon(shownCode,wx.isDay),WEATHER_ICON_W,WEATHER_ICON_H,INK);
 if(fresh&&isfinite(wx.temp)){
  int n=lroundf(wx.temp);snprintf(v,sizeof(v),"%d",abs(n));int right=768-getStringWidth("°",DEG)-5;
  drawString(768,86,"°",DEG,RIGHT);drawString(right,151,v,HERO,RIGHT);
  if(n<0)drawString(right-getStringWidth(v,HERO)-7,130,"−",MINUS,RIGHT);
 }else drawString(768,151,"—",HERO,RIGHT);
 drawString(weatherCenterX,185,fresh?(shownCode==45&&wx.code!=45&&wx.code!=48?"Możliwa mgła":description(shownCode)):"Brak danych",DESC,CENTER);
 number(v,sizeof(v),fresh?wx.feels:NAN);drawString(768,188,v,FEELS,RIGHT);
 drawString(768-getStringWidth(v,FEELS)-8,188,"Odczuwalna",LABEL,RIGHT);
 fb.fillRect(32,210,736,2,INK);fb.fillRect(32,275,736,2,INK);
 bool urgent;makeSummary(now,b,sizeof(b),urgent);
 char nearby[180];bool freezing=false;
 bool showNearby=!urgent&&nearRainMessage(now,nearby,sizeof(nearby),freezing);
 if(showNearby){snprintf(b,sizeof(b),"%s",nearby);urgent=freezing;}
 if(getStringWidth(b,SUMMARY_S)>736){
  // Skroc koncowke, zachowujac rzeczowe zdanie i stalą minimalna czcionke.
  char *p=strstr(b,". ");if(p)*p=0;
 }
 if(getStringWidth(b,SUMMARY_S)>736)snprintf(b,sizeof(b),"%s",urgent?"Trudne warunki pogodowe":"Zmienna pogoda w ciągu dnia");
 char detail[180];summaryDetail(now,detail,sizeof(detail));
 if(showNearby&&freezing)snprintf(detail,sizeof(detail),"Może być ślisko na drogach i chodnikach");
 // Zyczenia swiateczne w drugim wierszu: tylko gdy nic nie ostrzega i wiersz jest wolny.
 if(!*detail&&!urgent&&fresh){
  SummaryContext c=summaryContext(now);
  const char *greeting=holidayGreeting(ti,!c.tomorrow&&(c.day.wetCount>0||showNearby),!c.tomorrow&&c.day.snow);
  if(*greeting&&getStringWidth(greeting,RAIN_S)<=736)snprintf(detail,sizeof(detail),"%s",greeting);
 }
 if(*detail){drawString(400,242,b,SUMMARY_S,CENTER);drawString(400,267,detail,RAIN_S,CENTER);}
 else drawString(400,254,b,getStringWidth(b,SUMMARY)<=736?SUMMARY:SUMMARY_S,CENTER);
 if(!fresh){drawString(400,361,"Czekam na aktualną pogodę",SUMMARY_S,CENTER);drawString(32,34,LOCATION_LABEL,DESC);return;}

 int indices[5],ys[5];float low=100,high=-100;
 time_t target=now-now%3600;
 for(int k=0;k<5;k++){
  indices[k]=-1;for(int j=0;j<wx.n;j++)if(wx.hours[j].at==target+k*10800){indices[k]=j;break;}
  if(indices[k]>=0&&isfinite(wx.hours[indices[k]].temp)){float t=wx.hours[indices[k]].temp;low=fminf(low,t);high=fmaxf(high,t);}
 }
 float mid=(low+high)/2,span=fmaxf(12,high-low+4);
 for(int k=0;k<5;k++){
  int j=indices[k];ys[k]=(j>=0&&isfinite(wx.hours[j].temp))?365-lroundf((wx.hours[j].temp-mid)*48/span):-1;
 }
 int midnightX=midnightGraphX(target);
 // Thin dotted guide below the temperature labels; no grey/dithering mode.
 if(midnightX>=0)for(int y=352;y<=400;y+=6)fb.fillRect(midnightX,y,1,2,INK);
 for(int k=1;k<5;k++)if(ys[k]>=0&&ys[k-1]>=0)for(int d=-1;d<=2;d++)fb.drawLine(80+(k-1)*160,ys[k-1]+d,80+k*160,ys[k]+d,INK);
 for(int k=0;k<5;k++){
  int x=80+k*160,j=indices[k];const Hour *h=j>=0?&wx.hours[j]:nullptr;
  if(ys[k]>=0){fb.fillCircle(x,ys[k],5,INK);number(v,sizeof(v),h->temp);drawString(x,ys[k]-21,v,POINT,CENTER);}
  else drawString(x,344,"—",POINT,CENTER);
  time_t ht=target+k*10800;tm from;localtime_r(&ht,&from);
  snprintf(v,sizeof(v),"%02d:%02d",from.tm_hour,from.tm_min);drawString(x,423,v,HOUR,CENTER);
 }
 for(int k=0;k<4;k++){
  int x=160+k*160;time_t ht=target+k*10800;
  RainBlock block=rainBlock(ht);screenRainBlockText(block,k,now,b,sizeof(b));
  if(rainBlockBlack(block)){fb.fillRoundRect(x-75,440,150,36,10,INK);textColor=PAPER;}
  const FontK &rainFont=getStringWidth(b,RAIN)>143?RAIN_S:RAIN;
  drawString(x,464,b,rainFont,CENTER);textColor=INK;

 }
 // Oprawa swiateczna na koncu: omija juz narysowana tresc. Ostrzezenie ja wylacza.
 bool deco=decorationsEnabled&&!urgent&&holidayDecoration(ti)!=DEC_NONE;
 if(!deco||decoShowsLabel(ti))drawString(32,34,LOCATION_LABEL,DESC);
 if(deco)drawHolidayDecoration(ti);
}
