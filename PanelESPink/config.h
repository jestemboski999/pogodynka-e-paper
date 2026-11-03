// ---------------------------------------------------------------------------
//  config.h  --  wszystkie ustawienia uzytkownika w jednym miejscu
// ---------------------------------------------------------------------------
#pragma once

// --- SIEC WiFi -------------------------------------------------------------
// Nazwa i haslo sieci sa w secrets/wifi.h (poza zwyklymi zrodlami):
//   #define WIFI_SSID "..."
//   #define WIFI_PASS "..."
#if __has_include("secrets/wifi.h")
#include "secrets/wifi.h"
#else
#error "Brak secrets/wifi.h z WIFI_SSID i WIFI_PASS"
#endif

// Nazwa urzadzenia w sieci (lista klientow routera). Litery, cyfry, myslnik.
#define WIFI_HOSTNAME   "Pogodynka-E-ink"

// --- STALY ADRES IP (opcjonalnie) ------------------------------------------
// DHCP jest domyslne. Przy statycznym IP wpisz adresy swojej sieci.
#define USE_STATIC_IP   0
#define IP_ADDR         192, 168, 1, 100
#define IP_GATEWAY      192, 168, 1, 1
#define IP_MASK         255, 255, 255, 0
#define IP_DNS1         192, 168, 1, 1
#define IP_DNS2         1, 1, 1, 1

// --- LOKALIZACJA -----------------------------------------------------------
// Przykladowe wspolrzedne: centrum Warszawy.
#define LOC_LAT         "52.2297"
#define LOC_LON         "21.0122"

// Strefa czasowa w formacie POSIX TZ (Polska: CET/CEST ze zmiana czasu)
#define TZ_INFO         "CET-1CEST,M3.5.0,M10.5.0/3"

// --- HARMONOGRAM ------------------------------------------------------------
// Zegar odswieza sie czesciowo co minute. Pogoda pobierana co WEATHER_EVERY_MIN
// minut. Wi-Fi to najwiekszy koszt energii: rok na 10 Ah wymaga wyraznie
// rzadszego pobierania (15-30 min) -- potwierdzic pomiarem pradu na baterii.
// Open-Meteo i tak aktualizuje biezace dane tylko co 15 min.
#define WEATHER_EVERY_MIN        15
#define WEATHER_MAX_BACKOFF_MIN  30   // najdluzsza przerwa po kolejnych bledach sieci
#define LOW_BATTERY_WEATHER_MIN  30   // odstep pobierania przy baterii <= 5%

// --- BATERIA ----------------------------------------------------------------
// Ponizej BATTERY_CRITICAL_V: bez Wi-Fi i zegara, tylko komunikat o ladowaniu
// i kontrola napiecia co CRITICAL_SLEEP_MIN. Powrot od BATTERY_RESUME_V.
// Korekta dzielnika/ADC (pomiar zanizal o ok. 1,5%). Sprawdzone 04.10.2026:
// woltomierz na baterii 4,005 V, plytka na samej baterii 4,008-4,010 V.
// Ponowna kalibracja: BATTERY_CAL = woltomierz / odczyt bez korekty.
#define BATTERY_CAL          1.015f
#define BATTERY_CRITICAL_V   3.30f
#define BATTERY_RESUME_V     3.45f
#define CRITICAL_SLEEP_MIN   30
// Zabezpieczenie: cykl dluzszy niz tyle sekund konczy sie wymuszonym snem.
#define CYCLE_WATCHDOG_S     90

// --- NIESPODZIANKI ------------------------------------------------------------
// Dzien urodzin domownika: tort w naglowku i zyczenia (0 = wylaczone).
#define BIRTHDAY_MONTH       0    // 1-12
#define BIRTHDAY_DAY         0    // 1-31

// --- KONTRAST PANELU -------------------------------------------------------
// GxEPD2 zanizza napiecie VCOM przy zapisie obrazu, przez co czern wychodzi
// szara. 1 = przywroc fabryczna wartosc przed pelnym odswiezeniem.
#define EPD_FIX_VCOM        1   // przywroc VCOM z OTP przed pelnym odswiezeniem
#define EPD_BOOSTER         1   // booster soft start jak w sterowniku Waveshare
#define EPD_FAST_VCOM_END   0   // VCOM w wysoka impedancje zaraz po LUT (rejestr 0x52)
// Wartosc rejestru 0x82. 0x2C = -2,3 V (fabryczna z OTP), 0x26 = -2,0 V,
// 0x1C = -1,5 V. Wieksza liczba to mocniejsza czern.
#define EPD_VCOM            0x2C
// Glebokie czyszczenie co N pelnych odswiezen (0 = wylaczone). Wlaczaj tylko,
// gdy sama korekta VCOM nie wystarczy -- dodaje dwa migniecia na cykl.
#define EPD_DEEP_CLEAN_EVERY 0

// --- DIAGNOSTYKA -----------------------------------------------------------
// 1 = po kazdym pelnym przerysowaniu wysyla zawartosc ekranu przez UART
//     (uzywane przez narzedzie tools/podglad.py do podgladu na PC).
//     Ustaw 0, gdy panel dziala docelowo -- oszczedza ~6 s na odswiezenie.
#define DEBUG_FB_DUMP       0
#define SERIAL_BAUD         460800
