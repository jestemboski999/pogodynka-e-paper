#pragma once
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
struct Hour { time_t at=0; float temp=NAN,prec=NAN,pop=NAN,gust=NAN,wind=NAN,visibility=NAN; int code=-1; };
struct Weather { bool valid=false; float temp=NAN,feels=NAN; int code=-1,isDay=1,n=0; time_t fetched=0,observed=0; Hour hours[48]; };
static Weather wx;
static bool stale(time_t now) { return !wx.valid || now-wx.fetched>5400 || now-wx.observed>7200 || wx.fetched>now+300 || wx.observed>now+300; }
static bool validWeatherCode(int c){switch(c){case 0:case 1:case 2:case 3:case 45:case 48:case 51:case 53:case 55:case 56:case 57:case 61:case 63:case 65:case 66:case 67:case 71:case 73:case 75:case 77:case 80:case 81:case 82:case 85:case 86:case 95:case 96:case 99:return true;default:return false;}}
static bool wetCode(int c){return validWeatherCode(c)&&c>=51;}
static bool snowCode(int c){return c==71||c==73||c==75||c==77||c==85||c==86;}
static bool hailCode(int c){return c==96||c==99;}
static bool rainCode(int c){return c==51||c==53||c==55||c==56||c==57||c==61||c==63||c==65||c==66||c==67||c==80||c==81||c==82;}
static bool freezeCode(int c){return c==56||c==57||c==66||c==67;}
// Tolerancja wylacznie na blad float przy dodawaniu, np. 0.06f+0.04f.
// 1e-7 mm jest znacznie ponizej rozdzielczosci danych; nie zerujemy sladow.
static bool measurablePrecip(float amount){return isfinite(amount)&&amount>=.1f-1e-7f;}
static bool hasAmount(const Hour &h){return measurablePrecip(h.prec);}
static bool expectsPrecip(const Hour &h){return hasAmount(h)||h.pop>=40||wetCode(h.code);}
struct RainBlock{float amount=0,pop=0;bool amountKnown=true,popKnown=true,wetSignal=false;};
static RainBlock rainBlock(time_t start){
 RainBlock b;
 for(int offset=0;offset<3;offset++){
  const Hour *found=nullptr;for(int j=0;j<wx.n;j++)if(wx.hours[j].at==start+offset*3600){found=&wx.hours[j];break;}
  if(!found||!isfinite(found->prec)||found->prec<0)b.amountKnown=false;else b.amount+=found->prec;
  if(found&&wetCode(found->code))b.wetSignal=true;
  if(!found||!isfinite(found->pop)||found->pop<0||found->pop>100)b.popKnown=false;else b.pop=fmaxf(b.pop,found->pop);
 }
 return b;
}
// Rozbieznosc nie jest pomiarem deszczu: zachowujemy surowa sume w logu.
static bool rainBlockUncertain(const RainBlock &b){return b.amountKnown&&b.amount==0&&(b.wetSignal||(b.popKnown&&b.pop>=70));}
static bool rainBlockBlack(const RainBlock &b){return b.amountKnown&&measurablePrecip(b.amount);}
static void rainBlockText(const RainBlock &b,char *out,size_t size){
 // Pokazuj uzyteczna szanse zamiast niejasnego pytania o ilosc.
 if(rainBlockUncertain(b)&&b.popKnown){snprintf(out,size,"Szansa %.0f%%",b.pop);return;}
 if(rainBlockUncertain(b)){snprintf(out,size,"Możliwe opady");return;}
 if(!b.amountKnown&&!b.popKnown){snprintf(out,size,"Brak danych");return;}
 char amount[24],pop[16];
 if(!b.amountKnown)snprintf(amount,sizeof(amount),"— mm");
 else if(b.amount<=0)snprintf(amount,sizeof(amount),"0 mm");
 else if(!measurablePrecip(b.amount))snprintf(amount,sizeof(amount),"<0,1 mm");
 else if(b.amount>999)snprintf(amount,sizeof(amount),">999 mm");
 else snprintf(amount,sizeof(amount),b.amount>=100?"%.0f mm":"%.1f mm",b.amount);
 for(char *p=amount;*p;p++)if(*p=='.')*p=',';
 if(b.amountKnown&&b.amount>0&&(b.amount<.1f||b.amount>=10)){
  char *gap=strchr(amount,' ');if(gap)memmove(gap,gap+1,strlen(gap));
 }
 if(!b.popKnown)snprintf(pop,sizeof(pop),"—%%");
 else if(b.pop>0&&b.pop<1)snprintf(pop,sizeof(pop),"<1%%");
 else snprintf(pop,sizeof(pop),"%.0f%%",b.pop);
 snprintf(out,size,b.amountKnown&&b.amount>999?"%s·%s":"%s · %s",amount,pop);
}
// Widocznosc jest chwilowa, w metrach; brak wartosci nie oznacza dobrej widocznosci.
static bool fogHour(const Hour &h){return h.code==45||h.code==48||(isfinite(h.visibility)&&h.visibility<1000);}
struct FogInfo {bool found=false,ongoing=false,endKnown=false,codeConfirmed=false;time_t first=0,end=0;float visibility=NAN,currentVisibility=NAN;};
static FogInfo fogInfo(time_t start,time_t end){
 FogInfo f;time_t expected=0;
 for(int i=0;i<wx.n;i++){
  const Hour &h=wx.hours[i];if(h.at+3600<=start||h.at>=end)continue;
  if(!f.found){if(!fogHour(h))continue;f.found=true;f.first=h.at;f.ongoing=h.at<=start;expected=h.at;}
  if(h.at!=expected)break;expected=h.at+3600;
  if(!fogHour(h)){
   if(isfinite(h.visibility)&&h.visibility>=1000&&h.code>=0){f.endKnown=true;f.end=h.at;}
   break;
  }
  if(f.ongoing&&h.at==f.first)f.currentVisibility=h.visibility;
  f.codeConfirmed|=h.code==45||h.code==48;
  if(isfinite(h.visibility))f.visibility=isfinite(f.visibility)?fminf(f.visibility,h.visibility):h.visibility;
 }
 // Biezacy kod mgly moze wyprzedzac prognoze godzinna. Nie zgadujemy konca.
 if((wx.code==45||wx.code==48)&&start>=wx.observed&&start-wx.observed<=7200&&!f.ongoing){
  f=FogInfo();f.found=f.ongoing=f.codeConfirmed=true;f.first=start;
 }
 return f;
}
static int localHour(time_t at){tm t;localtime_r(&at,&t);return t.tm_hour;}
static void fogTitle(const FogInfo &f,char *out,size_t size){
 const char *what=f.codeConfirmed?"Mgła":"Możliwa mgła";
 if(f.ongoing&&f.endKnown)snprintf(out,size,"%s do ok. %d:00",what,localHour(f.end));
 else if(!f.ongoing&&f.endKnown)snprintf(out,size,"%s ok. %d:00–%d:00",what,localHour(f.first),localHour(f.end));
 else if(!f.ongoing)snprintf(out,size,"%s od ok. %d:00",what,localHour(f.first));
 else snprintf(out,size,"%s. Uważaj na drodze",what);
}
static void fogDetailFor(const FogInfo &f,char *out,size_t size){
 out[0]=0;if(!f.found)return;
 if(!isfinite(f.visibility)){snprintf(out,size,"Widoczność: brak danych");return;}
 char v[32];if(f.visibility<100)snprintf(v,sizeof(v),"poniżej 100 m");
 else snprintf(v,sizeof(v),"ok. %.0f m",roundf(f.visibility/100)*100);
 if(f.ongoing)snprintf(out,size,"Widoczność miejscami %s",v);
 else snprintf(out,size,"Ok. %d:00 widoczność spadnie %s%s",localHour(f.first),f.visibility<100?"":"do ",v);
}
static void fogDetail(time_t now,char *out,size_t size){
 out[0]=0;if(stale(now))return;tm end;localtime_r(&now,&end);end.tm_hour=end.tm_min=end.tm_sec=0;end.tm_mday++;end.tm_isdst=-1;
 fogDetailFor(fogInfo(now,mktime(&end)),out,size);
}
static int displayCode(time_t now){
 if(wx.code==45||wx.code==48||wx.code>=51)return wx.code;
 for(int i=0;i<wx.n;i++)if(wx.hours[i].at<=now&&wx.hours[i].at+3600>now&&fogHour(wx.hours[i]))return 45;
 return wx.code;
}
struct DayInfo {
 bool complete=true, thunder=false,freezing=false,snow=false,fog=false,dry=true;
 bool hail=false,ice=false,heavySnow=false,heavyRain=false,snowWind=false,rain=false,rime=false,drizzleOnly=true;
 time_t iceAt=0,freezeAt=0,hailAt=0,snowAt=0,rainAt=0,coldAt=0,thunderAt=0,heavyRainAt=0,heavySnowAt=0,gustAt=0,hiAt=0,loAt=0;
 int count=0,wetCount=0,wetGaps=0,windHours=0,cloud=0; time_t first=0,last=0;
 float gust=0,hi=-100,lo=100,total=0,peak=0; bool rainCertain=false,firstCertain=false;int firstCode=-1,certainCount=0;
};
static DayInfo assess(time_t start,time_t end) {
 DayInfo d; time_t expected=start-start%3600;time_t lastWet=0;
 for(int j=0;j<wx.n;j++) {
  const Hour &h=wx.hours[j]; if(h.at+3600<=start||h.at>=end)continue;
  if(h.at!=expected){d.complete=false;lastWet=0;} expected=h.at+3600;d.count++;
  if(!isfinite(h.temp)||!isfinite(h.prec)||!isfinite(h.pop)||!isfinite(h.gust)||!validWeatherCode(h.code))d.complete=false;
  if(isfinite(h.temp)){if(h.temp>d.hi){d.hi=h.temp;d.hiAt=h.at;}if(h.temp<d.lo){d.lo=h.temp;d.loAt=h.at;}}
  if(isfinite(h.gust)){d.gust=fmaxf(d.gust,h.gust);if(h.gust>=60&&!d.gustAt)d.gustAt=h.at;}
  if(h.gust>=40)d.windHours++;
  d.hail|=hailCode(h.code);if(hailCode(h.code)&&!d.hailAt)d.hailAt=h.at;
  if(h.code>=95&&!d.thunderAt)d.thunderAt=h.at;
  d.heavySnow|=h.code==75||h.code==86;d.heavyRain|=h.code==65||h.code==82;
  if((h.code==75||h.code==86)&&!d.heavySnowAt)d.heavySnowAt=h.at;
  if((h.code==65||h.code==82)&&!d.heavyRainAt)d.heavyRainAt=h.at;
  d.snowWind|=snowCode(h.code)&&h.gust>=40;
  d.rime|=h.code==48;
  if(snowCode(h.code)&&!d.snowAt)d.snowAt=h.at;
  if(rainCode(h.code)){d.rain=true;if(!d.rainAt)d.rainAt=h.at;}
  if(freezeCode(h.code)&&!d.freezeAt)d.freezeAt=h.at;
  if(isfinite(h.temp)&&h.temp<0&&!d.coldAt)d.coldAt=h.at;
  // Tylko ryzyko: opad przy temperaturze <=0 lub ochlodzenie w 3 h po opadzie.
  // Brak historii przed poczatkiem prognozy nie jest zastepowany domyslem.
  bool liquid=(hasAmount(h)&&!snowCode(h.code))||rainCode(h.code);
  if(isfinite(h.temp)&&h.temp<=0&&(liquid||(lastWet&&h.at-lastWet<=10800))){d.ice=true;if(!d.iceAt)d.iceAt=h.at;}
  if(liquid||(snowCode(h.code)&&hasAmount(h)&&h.temp>0))lastWet=h.at;
  d.thunder|=h.code>=95;d.freezing|=freezeCode(h.code);d.fog|=fogHour(h);d.cloud+=h.code==3;
  bool wet=expectsPrecip(h);
  if(wet){d.drizzleOnly&=h.code==51||h.code==53||h.code==55;
   if(!d.first){d.first=h.at;d.firstCertain=hasAmount(h)||wetCode(h.code);d.firstCode=h.code;}
   else if(d.last!=h.at)d.wetGaps++;
   if(hasAmount(h)||wetCode(h.code))d.certainCount++;d.last=h.at+3600;d.wetCount++; d.snow|=snowCode(h.code); d.rainCertain|=hasAmount(h)||wetCode(h.code); }
  if(isfinite(h.prec)){d.total+=h.prec;d.peak=fmaxf(d.peak,h.prec);}
  d.dry&=isfinite(h.prec)&&h.prec==0&&isfinite(h.pop)&&h.pop<=20&&!wetCode(h.code);
 }
 if(expected<end||!d.count)d.complete=false;
 return d;
}
// Dopisuje godzine tylko wtedy, gdy zjawisko zaczyna sie pozniej niz w ciagu godziny.
static void timedMessage(const char *message,time_t at,time_t start,char *out,size_t size){
 if(at>start+3600)snprintf(out,size,"%s od ok. %d:00",message,localHour(at));
 else snprintf(out,size,"%s",message);
}
static void mergeCurrent(DayInfo &d,time_t now){
 d.thunder|=wx.code==95||hailCode(wx.code);d.freezing|=freezeCode(wx.code);d.hail|=hailCode(wx.code);
 d.heavySnow|=wx.code==75||wx.code==86;d.heavyRain|=wx.code==65||wx.code==82;d.rime|=wx.code==48;
 d.fog|=wx.code==45||wx.code==48;
 // Biezacy kod oznacza zjawisko teraz: bez godziny w komunikacie.
 if(wx.code>=95)d.thunderAt=0;
 if(hailCode(wx.code))d.hailAt=0;
 if(freezeCode(wx.code))d.freezeAt=0;
 if(wx.code==75||wx.code==86)d.heavySnowAt=0;
 if(wx.code==65||wx.code==82)d.heavyRainAt=0;
 if(wetCode(wx.code)){
  d.dry=false;d.rainCertain=true;d.snow|=snowCode(wx.code);d.rain|=rainCode(wx.code);
  if(snowCode(wx.code)&&(!d.snowAt||d.snowAt>now))d.snowAt=now;
  if(rainCode(wx.code)&&(!d.rainAt||d.rainAt>now))d.rainAt=now;
  time_t hourEnd=now-now%3600+3600;
  if(!d.first){d.first=now;d.last=hourEnd;d.firstCode=wx.code;d.firstCertain=true;d.wetCount++;}
  else if(d.first>now){if(d.first>hourEnd)d.wetGaps++;d.first=now;d.firstCode=wx.code;d.firstCertain=true;d.wetCount++;}
  d.drizzleOnly&=wx.code==51||wx.code==53||wx.code==55;
 }
}
struct SummaryContext{DayInfo day;FogInfo fog;tm date;time_t start,end;bool tomorrow=false;};
static SummaryContext summaryContext(time_t now){
 SummaryContext c;localtime_r(&now,&c.date);tm end=c.date;end.tm_hour=end.tm_min=end.tm_sec=0;end.tm_mday++;end.tm_isdst=-1;
 c.start=now;c.end=mktime(&end);c.day=assess(now,c.end);mergeCurrent(c.day,now);c.fog=fogInfo(now,c.end);
 const DayInfo &d=c.day;
 if(c.date.tm_hour>=21&&d.complete&&d.dry&&!d.thunder&&!d.freezing&&!d.heavySnow&&!d.ice&&d.gust<40&&d.lo>0&&!d.fog){
  tm next=end;next.tm_mday++;next.tm_isdst=-1;time_t nextEnd=mktime(&next);DayInfo n=assess(c.end,nextEnd);
  if(n.complete){c.start=c.end;c.end=nextEnd;c.date=end;c.day=n;c.fog=fogInfo(c.start,c.end);c.tomorrow=true;}
 }
 return c;
}
// "Jutro" wchodzi w zdanie, wiec pierwsza litera komunikatu musi byc mala (takze polska).
static void lowerFirst(char *s){
 unsigned char *u=(unsigned char*)s;
 if(u[0]>='A'&&u[0]<='Z'){u[0]+=32;return;}
 if(u[0]==0xC5&&(u[1]==0x9A||u[1]==0x81||u[1]==0xBB||u[1]==0xB9||u[1]==0x83))u[1]++;
 else if(u[0]==0xC4&&(u[1]==0x86||u[1]==0x84||u[1]==0x98))u[1]++;
 else if(u[0]==0xC3&&u[1]==0x93)u[1]=0xB3;
}
// Opad jako przedzial czasu: "Deszcz ok. 14:00–17:00", "Śnieg do ok. 9:00".
static void precipMessage(const DayInfo &d,time_t start,time_t end,bool tomorrow,char *msg,size_t size){
 if(d.snow&&d.rain){snprintf(msg,size,"%s",d.rainAt<d.snowAt?"Deszcz, później śnieg":"Śnieg, później deszcz");return;}
 bool heavy=(d.peak>=7.5||d.heavyRain)&&!d.snow;
 const char *kind=heavy?"Intensywny deszcz":d.drizzleOnly?"Mżawka":!d.firstCertain?"Możliwe opady":
  snowCode(d.firstCode)?"Śnieg":rainCode(d.firstCode)?"Deszcz":"Opady";
 if(d.wetGaps>0&&!heavy&&!d.drizzleOnly)
  kind=!d.firstCertain?"Możliwe przelotne opady":snowCode(d.firstCode)?"Przelotny śnieg":rainCode(d.firstCode)?"Przelotny deszcz":"Przelotne opady";
 bool ongoing=d.first<=start,toEnd=d.last>=end;
 if(ongoing&&toEnd)snprintf(msg,size,"%s %s",kind,tomorrow?"przez cały dzień":"do końca dnia");
 else if(ongoing)snprintf(msg,size,"%s do ok. %d:00",kind,localHour(d.last));
 else if(toEnd||d.wetGaps>0)snprintf(msg,size,"%s od ok. %d:00",kind,localHour(d.first));
 else snprintf(msg,size,"%s ok. %d:00–%d:00",kind,localHour(d.first),localHour(d.last));
}
static void makeSummary(time_t now,char *out,size_t size,bool &urgent) {
 urgent=false;
 if(stale(now)){snprintf(out,size,"Brak świeżej prognozy");return;}
 SummaryContext c=summaryContext(now);DayInfo &d=c.day;FogInfo &fog=c.fog;
 bool tomorrow=c.tomorrow;tm &t=c.date;time_t start=c.start;char msg[150];
 // Zagrozenia wykryte nawet w niepelnej prognozie nie znikaja za ogolnym bledem.
 if(d.hail){timedMessage("Możliwe burze z gradem",d.hailAt,start,msg,sizeof(msg));urgent=true;}
 else if(d.thunder){timedMessage("Możliwe burze",d.thunderAt,start,msg,sizeof(msg));urgent=true;}
 else if(d.freezing){
  if(d.freezeAt>start+3600)timedMessage("Marznące opady",d.freezeAt,start,msg,sizeof(msg));
  else snprintf(msg,sizeof(msg),"Marznące opady. Możliwa gołoledź");
  urgent=true;
 }
 else if(d.gust>=60){
  if(d.gustAt>start+3600)snprintf(msg,sizeof(msg),"Od ok. %d:00 silny wiatr, porywy do %.0f km/h",localHour(d.gustAt),d.gust);
  else snprintf(msg,sizeof(msg),"Silny wiatr, porywy do %.0f km/h",d.gust);
  urgent=true;
 }
 else if(fog.ongoing&&isfinite(fog.visibility)&&fog.currentVisibility<200){snprintf(msg,sizeof(msg),"Bardzo słaba widoczność");urgent=true;}
 else if(d.ice){timedMessage("Możliwe oblodzenie",d.iceAt,start,msg,sizeof(msg));urgent=true;}
 else if(d.heavySnow){timedMessage("Intensywne opady śniegu",d.heavySnowAt,start,msg,sizeof(msg));urgent=true;}
 else if(d.snowWind){snprintf(msg,sizeof(msg),"Śnieg i silny wiatr");urgent=true;}
 else if(d.heavyRain){timedMessage("Intensywne opady deszczu",d.heavyRainAt,start,msg,sizeof(msg));urgent=true;}
 else if(!d.complete){snprintf(msg,sizeof(msg),"Brak pełnej prognozy na dziś");}
 else if(d.total>0&&d.wetCount==0){snprintf(msg,sizeof(msg),"Najwyżej śladowe opady");}
 else if(d.wetCount)precipMessage(d,start,c.end,tomorrow,msg,sizeof(msg));
 else if(d.rime){snprintf(msg,sizeof(msg),"Mgła osadzająca szadź");}
 else if(fog.found)fogTitle(fog,msg,sizeof(msg));
 else if(d.windHours>=2)snprintf(msg,sizeof(msg),"Porywy wiatru do %.0f km/h",d.gust);
 else if(d.hi>=30)snprintf(msg,sizeof(msg),"Upał do %.0f°. Pamiętaj o wodzie",d.hi);
 else if(d.hi<0)snprintf(msg,sizeof(msg),"Mroźny dzień. Ubierz się ciepło");
 else if(d.lo<0)timedMessage("Przymrozek",d.coldAt,start,msg,sizeof(msg));
 else if(d.fog)snprintf(msg,sizeof(msg),"Miejscami mgła");
 else if(d.hi-d.lo>=6&&d.count>2){
  // Kierunek zmiany: rano "do 24°", wieczorem "ochłodzenie do 12°".
  bool cooling=d.loAt>d.hiAt;float value=cooling?d.lo:d.hi;
  if(d.dry)snprintf(msg,sizeof(msg),cooling?"Bez opadów, ochłodzenie do %.0f°":"Bez opadów, do %.0f°",value);
  else snprintf(msg,sizeof(msg),cooling?"Ochłodzenie do %.0f°, niewielka szansa opadów":"Do %.0f°, niewielka szansa opadów",value);
 }
 else if(d.dry){
  int hour=localHour(now);
  if((t.tm_yday+t.tm_year)%7==0)snprintf(msg,sizeof(msg),"Sucho. Parasol zostaje w domu");
  else if(d.hi<12)snprintf(msg,sizeof(msg),"Chłodno i bez opadów");
  else if(d.cloud>d.count/2)snprintf(msg,sizeof(msg),"Pochmurno, ale bez opadów");
  else if(tomorrow||(hour>=5&&hour<18))snprintf(msg,sizeof(msg),"Spokojny dzień bez opadów");
  else if(hour>=18&&hour<22)snprintf(msg,sizeof(msg),"Spokojny wieczór bez opadów");
  else snprintf(msg,sizeof(msg),"Spokojna noc bez opadów");
 }else snprintf(msg,sizeof(msg),"Niewielka szansa opadów");
 if(tomorrow){lowerFirst(msg);snprintf(out,size,"Jutro %s",msg);}
 else snprintf(out,size,"%s",msg);
}

// Drugi wiersz zachowuje istotne ryzyko, gdy glowny komunikat zajela burza.
static void summaryDetail(time_t now,char *out,size_t size){
 out[0]=0;if(stale(now))return;
 SummaryContext c=summaryContext(now);DayInfo &d=c.day;
 if(d.freezing){snprintf(out,size,"Może być ślisko na drogach i chodnikach");return;}
 if(d.ice){snprintf(out,size,"Mokre drogi i chodniki mogą zamarzać");return;}
 if(d.snowWind){snprintf(out,size,"Śnieg i wiatr mogą ograniczać widoczność");return;}
 if(d.hail){snprintf(out,size,"Możliwy grad. Schowaj rzeczy z balkonu");return;}
 if(d.heavySnow){snprintf(out,size,"Śnieg może utrudniać poruszanie się");return;}
 fogDetailFor(c.fog,out,size);
}
