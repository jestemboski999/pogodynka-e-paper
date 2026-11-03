#pragma once
#include <math.h>
#include <time.h>
// Przyblizona krzywa spoczynkowa typowego Li-ion/LiPo 1S, 4.2 V.
// Nie jest kalibracja konkretnego pakietu ani pomiarem przeplywu ladunku.
static float batteryVoltage=NAN;
static int batteryPercent=-1;
static float batterySoc(float volts){
 if(!isfinite(volts)||volts<2.8f||volts>4.3f)return NAN;
 static const float v[]={3.00f,3.30f,3.50f,3.60f,3.70f,3.75f,3.80f,3.85f,3.90f,3.95f,4.00f,4.10f,4.20f};
 static const float p[]={0,2,5,10,20,30,40,50,60,70,80,90,100};
 if(volts<=v[0])return 0;
 for(int i=1;i<13;i++)if(volts<=v[i])return p[i-1]+(volts-v[i-1])*(p[i]-p[i-1])/(v[i]-v[i-1]);
 return 100;
}
static void acceptBatteryVoltage(float measured){
 if(!isfinite(batterySoc(measured))){batteryVoltage=NAN;batteryPercent=-1;return;}
 // Samples are filtered within one measurement. Do not average measurements
 // twelve hours apart: that would delay a genuinely discharged battery.
 batteryVoltage=measured;
 int next=(int)lroundf(batterySoc(batteryVoltage));
 // Histereza 2 pp ogranicza skakanie wskaznika przy plaskiej krzywej.
 if((next<=5&&batteryPercent>5)||batteryPercent<0||abs(next-batteryPercent)>=2||next==0||next==100)batteryPercent=next;
}
