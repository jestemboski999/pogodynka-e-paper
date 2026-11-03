#pragma once
#include <ArduinoJson.h>
static float jsonNumber(JsonVariantConst v){
 if(!v.is<float>())return NAN;float n=v.as<float>();return isfinite(n)?n:NAN;
}
static float boundedNumber(JsonVariantConst v,float low,float high){float n=jsonNumber(v);return isfinite(n)&&n>=low&&n<=high?n:NAN;}
static int jsonWeatherCode(JsonVariantConst v){if(!v.is<int>())return -1;int c=v.as<int>();return validWeatherCode(c)?c:-1;}
static bool decodeWeather(JsonVariantConst doc,time_t now,Weather &next){
 JsonObjectConst cur=doc["current"],h=doc["hourly"];
 if(cur.isNull()||h.isNull()||!cur["time"].is<int64_t>()||h["time"].size()<16)return false;
 // Kopia z pustego wzorca zamiast tymczasowego Weather() -- 2 kB mniej na stosie.
 static const Weather empty;next=empty;next.observed=cur["time"].as<time_t>();
 if(next.observed>now+300||now-next.observed>7200)return false;
 next.temp=boundedNumber(cur["temperature_2m"],-100,70);next.feels=boundedNumber(cur["apparent_temperature"],-150,100);
 next.code=jsonWeatherCode(cur["weather_code"]);int day=cur["is_day"].is<int>()?cur["is_day"].as<int>():-1;next.isDay=(day==0||day==1)?day:-1;
 next.n=min(48,(int)h["time"].size()-1);
 bool coversStart=false,coversEnd=false;time_t start=now-now%3600;
 for(int i=0;i<next.n;i++){
  Hour &a=next.hours[i];if(!h["time"][i].is<int64_t>())return false;a.at=h["time"][i].as<time_t>();
  if(a.at<now-172800||a.at>now+259200||(i&&a.at!=next.hours[i-1].at+3600))return false;
  // Sprawdzamy takze timestamp konca ostatniego przedzialu opadu.
  if(!h["time"][i+1].is<int64_t>()||h["time"][i+1].as<time_t>()!=a.at+3600)return false;
  a.temp=boundedNumber(h["temperature_2m"][i],-100,70);a.code=jsonWeatherCode(h["weather_code"][i]);
  a.visibility=boundedNumber(h["visibility"][i],0,1000000);a.wind=boundedNumber(h["wind_speed_10m"][i],0,500);
  a.prec=boundedNumber(h["precipitation"][i+1],0,2000);a.pop=boundedNumber(h["precipitation_probability"][i+1],0,100);
  a.gust=boundedNumber(h["wind_gusts_10m"][i+1],0,500);
  coversStart|=a.at==start;coversEnd|=a.at==start+14*3600;
 }
 if(!coversStart||!coversEnd)return false;
 next.fetched=now;next.valid=true;return true;
}
