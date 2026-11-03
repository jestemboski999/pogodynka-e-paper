#pragma once
#include <stdint.h>
#include <time.h>
#ifndef WEATHER_EVERY_MIN
#define WEATHER_EVERY_MIN 15
#endif
#ifndef LOW_BATTERY_WEATHER_MIN
#define LOW_BATTERY_WEATHER_MIN 30
#endif
// 800x480, 1 bit/pixel. Pomijamy tylko zegar i znacznik aktualizacji i baterie.
static uint64_t contentFingerprint(const uint8_t *pixels){
 uint64_t hash=14695981039346656037ULL;
 for(int y=0;y<480;y++)for(int byte=0;byte<100;byte++){
  if(y>=50&&y<162&&byte>=2&&byte<44)continue;
  if(y<40&&byte>=67)continue;
  if(y>=462&&byte>=90)continue;
  hash^=pixels[y*100+byte];hash*=1099511628211ULL;
 }
 return hash;
}
static bool fullRefreshNeeded(bool resumed,bool valid,uint64_t previous,uint64_t current,time_t now,time_t lastFull){
 return !resumed||!valid||previous!=current||now<lastFull||now-lastFull>=3600;
}
// Harmonogram pobierania. Przy niskiej baterii rzadziej, by oszczedzac radio.
static int fetchIntervalSeconds(bool lowBattery){
 int minutes=WEATHER_EVERY_MIN;
 if(lowBattery&&minutes<LOW_BATTERY_WEATHER_MIN)minutes=LOW_BATTERY_WEATHER_MIN;
 return minutes*60;
}
// Po kolejnych bledach przerwa rosnie: 1, 2, 4, 8... odstepow, najwyzej maxSeconds.
static int retryDelaySeconds(int failures,int interval,int maxSeconds){
 if(failures<=1)return interval;
 long delay=(long)interval<<(failures-1>10?10:failures-1);
 if(delay>maxSeconds)delay=maxSeconds;
 return delay<interval?interval:(int)delay;
}
// Kolejne pobranie wypada na granicy odstepu (np. 00, 05, 10 min).
static time_t nextFetchTime(time_t now,int failures,int interval,int maxSeconds){
 int steps=retryDelaySeconds(failures,interval,maxSeconds)/interval;
 if(steps<1)steps=1;
 return (now/interval+steps)*interval;
}
// Odliczanie nowcastu ("Deszcz za ok. 20 min") przeliczane co 5 min bez Wi-Fi,
// bo pogoda pobierana jest rzadziej niz zmienia sie ten komunikat.
static bool nowcastRecompose(bool nowcastActive,time_t now,time_t lastCompose){
 return nowcastActive&&now/300!=lastCompose/300;
}
static bool weatherDue(bool resumed,bool clockSet,time_t now,time_t lastAttempt,time_t nextAt){
 return !resumed||!clockSet||now<lastAttempt||now>=nextAt;
}
