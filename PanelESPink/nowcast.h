#pragma once
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "podsumowanie.h"
struct NearRainPoint { time_t at=0; float rate=0; int type=0; };
struct NearRain { time_t fetched=0; int n=0; NearRainPoint points[25]; };
static NearRain nearRain;
// UTC only: never interpret API timestamps in the local timezone.
static time_t nearRainTime(const char *s){
 if(!s||strlen(s)!=20)return 0;
 int y,m,d,h,mi,se;char tail=0;
 if(sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d%c",&y,&m,&d,&h,&mi,&se,&tail)!=7||tail!='Z')return 0;
 if(y<2024||y>2100||m<1||m>12||d<1||d>31||h<0||h>23||mi<0||mi>59||se<0||se>59)return 0;
 static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 int limit=days[m-1]+(m==2&&(y%4==0&&(y%100!=0||y%400==0)));
 if(d>limit)return 0;
 int yy=y-(m<=2),era=yy/400;unsigned yo=yy-era*400;
 unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 return (time_t)(((int64_t)era*146097+yo*365+yo/4-yo/100+doy-719468)*86400+h*3600+mi*60+se);
}
static bool decodeNearRain(JsonVariantConst doc,time_t now,NearRain &out){
 JsonArrayConst timelines=doc["data"]["timelines"].as<JsonArrayConst>();
 if(timelines.size()!=1||strcmp(timelines[0]["timestep"]|"","5m"))return false;
 JsonArrayConst rows=timelines[0]["intervals"].as<JsonArrayConst>();
 if(rows.size()<13||rows.size()>25)return false;
 NearRain next;next.fetched=now;
 for(JsonObjectConst row:rows){
  NearRainPoint p;p.at=nearRainTime(row["startTime"]|"");
  JsonVariantConst rate=row["values"]["precipitationIntensity"],type=row["values"]["precipitationType"];
  if(!p.at||!rate.is<float>())return false;
  p.rate=rate.as<float>();p.type=type.is<int>()?type.as<int>():0;
  if(!isfinite(p.rate)||p.rate<0||p.rate>500||p.type<0||p.type>4)return false;
  if(next.n&&p.at!=next.points[next.n-1].at+300)return false;
  next.points[next.n++]=p;
 }
 if(next.points[0].at>now+60||next.points[0].at<now-600||next.points[next.n-1].at<now+3300)return false;
 out=next;return true;
}
static bool nearRainFresh(time_t now){
 return nearRain.n>=13&&nearRain.fetched<=now&&now-nearRain.fetched<=1200&&
  nearRain.points[0].at<=now&&nearRain.points[nearRain.n-1].at>=now+300;
}
// Only positive precipitation signals override the daily forecast. No data
// or a dry short-term forecast must never mean a guarantee of a dry day.
static bool nearRainMessage(time_t now,char *out,size_t size,bool &freezing){
 out[0]=0;freezing=false;if(!nearRainFresh(now))return false;
 int current=0;while(current+1<nearRain.n&&nearRain.points[current+1].at<=now)current++;
 int first=-1;
 for(int i=current;i+1<nearRain.n&&nearRain.points[i].at<=now+5400;i++){
  if(nearRain.points[i].rate>=.1f&&nearRain.points[i+1].rate>=.1f){first=i;break;}
 }
 if(first<0)return false;
 int type=nearRain.points[first].type;freezing=type==3;
 const char *kind=type==1?"Deszcz":type==2?"Śnieg":type==3?"Marznące opady":"Opady";
 int stop=-1;
 for(int i=first+1;i+1<nearRain.n;i++)if(nearRain.points[i].rate<.1f&&nearRain.points[i+1].rate<.1f){stop=i;break;}
 char until[32]="";
 if(stop>=0){tm t;time_t end=(nearRain.points[stop].at+150)/300*300;localtime_r(&end,&t);snprintf(until,sizeof(until),"do ok. %d:%02d",t.tm_hour,t.tm_min);}
 if(first>current){
  int minutes=(int)((nearRain.points[first].at-now+150)/300)*5;
  char when[40];
  if(minutes<10)snprintf(when,sizeof(when),"w ciągu kilku minut");
  else snprintf(when,sizeof(when),"za ok. %d min",minutes);
  if(*until)snprintf(out,size,"%s %s, %s",kind,when,until);
  else snprintf(out,size,"%s %s",kind,when);
 }else if(*until)snprintf(out,size,"%s %s",kind,until);
 else{
  // Brak konca w horyzoncie: mowimy, jak dlugo prognoza na pewno siega.
  time_t last=nearRain.points[nearRain.n-1].at;
  if(last-now>=6300)snprintf(out,size,"%s przez najbliższe 2 godziny",kind);
  else{tm t;last=last/300*300;localtime_r(&last,&t);snprintf(out,size,"%s co najmniej do %d:%02d",kind,t.tm_hour,t.tm_min);}
 }
 return true;
}
// Czy warto pytac o prognoze na 2 h? Tylko gdy dzienna prognoza dopuszcza
// opad w najblizszych 3 h, pada teraz albo poprzedni nowcast widzial opad.
// Brak danych nie wyklucza opadu, wiec wtedy pytamy.
static bool nearRainUseful(time_t now,bool lowBattery){
 if(lowBattery)return false;
 if(stale(now)||wetCode(wx.code))return true;
 char unused[8];bool freezing;
 if(nearRainMessage(now,unused,sizeof(unused),freezing))return true;
 bool covered=false;
 for(int i=0;i<wx.n;i++){
  const Hour &h=wx.hours[i];if(h.at+3600<=now||h.at>=now+3*3600)continue;
  covered=true;
  if(!isfinite(h.pop)||!isfinite(h.prec)||h.pop>0||h.prec>0||wetCode(h.code)||h.code<0)return true;
 }
 return !covered;
}
