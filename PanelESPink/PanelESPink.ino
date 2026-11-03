// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Filip (jestemboski999) for Pogodynka C Studio contributions.
// C Studio: oddzielny program, zachowany sterownik panelu i ustawienia sieci.
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>

#include "config.h"
#include "tomorrow_ca.h"
#if __has_include("secrets/tomorrow_key.h")
#include "secrets/tomorrow_key.h"
#else
#define TOMORROW_API_KEY ""
#endif

// LaskaKit ESPink v3.x: oficjalny przyklad Erase_screen.
#if !CONFIG_IDF_TARGET_ESP32S3
#error "Wybierz ESP32S3 Dev Module"
#endif
#define EPD_CS   10
#define EPD_DC   48
#define EPD_RST  45
#define EPD_BUSY 38
#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_MISO -1
#define EPD_POWER 47

// Bufor GxEPD2 celowo maly (1/4 ekranu = 12 kB): pelny obraz i tak powstaje
// we wlasnej kanwie, a ESP32 ma twardy limit ~180 kB na dane statyczne.
//
// GxEPD2 obniza VCOM (rejestr 0x82) do -2,0 V w _Init_Part() i nigdy go nie
// przywraca, a _writeImage() wola _Init_Part() przy KAZDYM zapisie obrazu.
// Zanizone napiecie trafia wiec takze na pelne odswiezenia i czern plowieje
// w ostatniej fazie napedu. Stad wlasna sekwencja pelnego odswiezenia.
// Adapted from GxEPD2_750_T7 by Jean-Marc Zingg (GxEPD2 1.6.9, GPLv3).
// Pogodynka modification (2026): custom full refresh with VCOM and booster options.
class GxEPD2_750_T7_VCOM : public GxEPD2_750_T7 {
 public:
  GxEPD2_750_T7_VCOM(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750_T7(cs, dc, rst, busy) {}

  void preserveReset(bool preserve){ _rst=preserve?-1:EPD_RST; }
  using GxEPD2_750_T7::refresh;   // nie chowaj wersji z prostokatem

  // Powtarza _Init_Full() + _InitDisplay() (prywatne w klasie bazowej) i dokłada:
  //  * rejestr 0x82 (VCOM_DC) z wartoscia fabryczna -- tuz przed odswiezeniem,
  //    bo wczesniej nadpisalby go _Init_Part(),
  //  * komende 0x06 (booster soft start), ktora wysyla sterownik Waveshare,
  //    a GxEPD2 dla wariantu T7 pomija.
  void refresh(bool partial_update_mode = false) {
    if (partial_update_mode) { GxEPD2_750_T7::refresh(0, 0, WIDTH, HEIGHT); return; }

    _writeCommand(0x01);                          // POWER SETTING
    _writeData(0x07); _writeData(0x07);           // VGH +20 V, VGL -20 V
    _writeData(0x3f); _writeData(0x3f);           // VDH +15 V, VDL -15 V
#if EPD_BOOSTER
    _writeCommand(0x06);                          // BOOSTER SOFT START
    _writeData(0x17); _writeData(0x17);
    _writeData(0x28); _writeData(0x17);
#endif
    _writeCommand(0x00); _writeData(0x1f);        // PANEL SETTING, LUT z OTP
    _writeCommand(0x61);                          // rozdzielczosc
    _writeData(WIDTH / 256);  _writeData(WIDTH % 256);
    _writeData(HEIGHT / 256); _writeData(HEIGHT % 256);
    _writeCommand(0x15); _writeData(0x00);
    _writeCommand(0x50); _writeData(0x29); _writeData(0x07);
    _writeCommand(0x60); _writeData(0x22);        // TCON
#if EPD_FIX_VCOM
    _writeCommand(0x82); _writeData(EPD_VCOM);    // wlasciwa poprawka
#endif
#if EPD_FAST_VCOM_END
    _writeCommand(0x52); _writeData(0x0A);        // VCOM w stan wysokiej impedancji
#endif                                            // zaraz po zakonczeniu LUT

    if (!_power_is_on) { _writeCommand(0x04); _waitWhileBusy("_PowerOn", 140); }
    _power_is_on = true;
    _using_partial_mode = false;

    _writeCommand(0x12);                          // DISPLAY REFRESH
    _waitWhileBusy("_Update_Full", 4200);
    _initial_refresh = false;
  }
};

GxEPD2_BW<GxEPD2_750_T7_VCOM, GxEPD2_750_T7_VCOM::HEIGHT / 4> display(
    GxEPD2_750_T7_VCOM(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------------------------------------------------------------------------
//  PLOTNO
// ---------------------------------------------------------------------------
#define DISP_WIDTH  800
#define DISP_HEIGHT 480
#define INK   1        // 1 = piksel zapalony = czarny na e-papierze
#define PAPER 0

// Bufor obrazu trzymamy w .bss, a nie na stercie: 48 kB ciaglej pamieci trudno
// dostac po starcie WiFi, a statyczna alokacja nie moze sie nie udac.
static uint8_t FB_MEM[(DISP_WIDTH / 8) * DISP_HEIGHT];

class StaticCanvas1 : public GFXcanvas1 {
 public:
  StaticCanvas1(uint16_t w, uint16_t h, uint8_t *mem) : GFXcanvas1(w, h, false) {
    buffer = mem;
    buffer_owned = false;
  }
};

StaticCanvas1 fb(DISP_WIDTH, DISP_HEIGHT, FB_MEM);

// Domyslne 8 kB stosu loopTask nie wystarcza przy pobieraniu (HTTP, TLS, Wi-Fi
// wolane z setup()); przepelnienie konczylo sie restartem zaraz po polaczeniu.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---------------------------------------------------------------------------
//  RYSOWANIE EKRANU -- typy danych i wszystkie sekcje ukladu. Dolaczone przed
//  pierwsza funkcja, bo Arduino wstrzykuje tam automatyczne prototypy.
// ---------------------------------------------------------------------------
#include "ekran.h"
#include "dane_pogody.h"
#include "odswiezanie.h"

// ===========================================================================
//  STAN W PAMIECI RTC -- przetrwa deep sleep, bez zapisow Flash w cyklu
// ===========================================================================
static const uint32_t CACHE_MAGIC = 0x45504237;
RTC_DATA_ATTR uint32_t cacheMagic = 0;
RTC_DATA_ATTR uint8_t  weatherCache[sizeof(Weather)];
RTC_DATA_ATTR uint8_t  nearRainCache[sizeof(NearRain)];
RTC_DATA_ATTR time_t   lastAttempt = 0, nextFetchAt = 0, lastScreenHour = 0, lastCompose = 0;
RTC_DATA_ATTR uint8_t  fetchFailures = 0;
RTC_DATA_ATTR bool     lastScreenStale = true;
RTC_DATA_ATTR uint64_t previousContent = 0;
RTC_DATA_ATTR bool     contentValid = false;
RTC_DATA_ATTR time_t   lastFullRefresh = 0;
RTC_DATA_ATTR float    savedBatteryVoltage = NAN;
RTC_DATA_ATTR int      savedBatteryPercent = -1;
RTC_DATA_ATTR int      renderedBatteryPercent = -1;
RTC_DATA_ATTR bool     criticalMode = false, criticalShown = false;
// Historia pomiarow baterii (24 h przy pobieraniu co 15 min), wypisywana w logu.
// Pozwala porownac odczyty na samej baterii i z USB bez restartu plytki.
struct BatteryLogEntry { uint32_t at; uint16_t pinMv, batMv; };
static const int BATTERY_LOG_SIZE = 96;
RTC_DATA_ATTR BatteryLogEntry batteryLog[BATTERY_LOG_SIZE];
RTC_DATA_ATTR uint8_t  batteryLogCount = 0, batteryLogHead = 0;
RTC_DATA_ATTR uint32_t fullRefreshCount = 0;
// Ostatni punkt dostepu: ponowne polaczenie bez skanowania wszystkich kanalow.
RTC_DATA_ATTR uint8_t  wifiBssid[6];
RTC_DATA_ATTR int32_t  wifiChannel = 0;
RTC_DATA_ATTR time_t   nearRainRetryAt = 0;

// ===========================================================================
//  WYSYLANIE OBRAZU DO PANELU
// ===========================================================================
// GxEPD2 dzieli okno na strony po pageHeight() wierszy i powtarza petle takze
// w drugiej fazie (zapis "poprzedniego" obrazu). Kopiujemy tylko wiersze
// biezacej strony -- reszta i tak zostalaby odcieta w drawPixel().
static void blitRows(int16_t x, int16_t y0, int16_t w, int16_t y1) {
  const uint8_t *buf = fb.getBuffer();
  const int stride = (DISP_WIDTH + 7) / 8;
  for (int16_t yy = y0; yy < y1; yy++) {
    const uint8_t *row = buf + (size_t)yy * stride;
    for (int16_t xx = x; xx < x + w; xx++) {
      bool on = row[xx >> 3] & (0x80 >> (xx & 7));
      display.drawPixel(xx, yy, on ? GxEPD_BLACK : GxEPD_WHITE);
    }
  }
}

static void pushWindow(int16_t x, int16_t y, int16_t w, int16_t h, bool full) {
  if (full) display.setFullWindow(); else display.setPartialWindow(x, y, w, h);
  const int pages = display.pages(), pageH = display.pageHeight();
  int page = 0;
  display.firstPage();
  do {
    int top = y + (page++ % pages) * pageH;
    int from = max<int>(top, y), to = min<int>(top + pageH, y + h);
    if (from < to) blitRows(x, from, w, to);
  } while (display.nextPage());
}

#if DEBUG_FB_DUMP
static void dumpFramebuffer() {
  static const char B64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *b = fb.getBuffer();
  const size_t n = (size_t)(DISP_WIDTH / 8) * DISP_HEIGHT;
  Serial.printf("\n---FB-BEGIN %d %d---\n", DISP_WIDTH, DISP_HEIGHT);
  char line[81];
  int li = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = ((uint32_t)b[i] << 16) | ((uint32_t)b[i + 1] << 8) | (uint32_t)b[i + 2];
    line[li++] = B64[(v >> 18) & 63];
    line[li++] = B64[(v >> 12) & 63];
    line[li++] = B64[(v >> 6) & 63];
    line[li++] = B64[v & 63];
    if (li >= 76) { line[li] = 0; Serial.println(line); li = 0; }
  }
  if (li) { line[li] = 0; Serial.println(line); }
  Serial.println("---FB-END---");
}
#endif

// Wi-Fi jest zawsze wylaczone przed odswiezeniem (koniec pobierania w setup()),
// wiec radio nie obciaza szyny zasilania panelu.
static void pushFull() {
#if EPD_DEEP_CLEAN_EVERY
  // Licznik w RTC: zwykla zmienna statyczna zerowalaby sie po kazdym wybudzeniu.
  if (fullRefreshCount % EPD_DEEP_CLEAN_EVERY == 0) {
    display.setFullWindow();
    display.firstPage();
    do { display.fillScreen(GxEPD_BLACK); } while (display.nextPage());
    display.firstPage();
    do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
  }
#endif
  fullRefreshCount++;
  pushWindow(0, 0, DISP_WIDTH, DISP_HEIGHT, true);
#if DEBUG_FB_DUMP
  dumpFramebuffer();
#endif
}

// ===========================================================================
//  SIEC
// ===========================================================================
static bool waitWifi(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(20);
  return WiFi.status() == WL_CONNECTED;
}

static bool connectWifi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.persistent(false);             // bez zapisu konfiguracji do NVS przy kazdym polaczeniu
  WiFi.setHostname(WIFI_HOSTNAME);    // nazwa widoczna na liscie klientow routera (DHCP)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
#if USE_STATIC_IP
  {
    IPAddress ip(IP_ADDR), gw(IP_GATEWAY), mask(IP_MASK), d1(IP_DNS1), d2(IP_DNS2);
    if (!WiFi.config(ip, gw, mask, d1, d2))
      Serial.println("[wifi] nie udalo sie ustawic stalego adresu IP");
  }
#endif
  uint32_t t0 = millis();
  bool ok = false;
  if (wifiChannel > 0) {
    // Szybka sciezka: znany kanal i BSSID, bez skanowania.
    WiFi.begin(WIFI_SSID, WIFI_PASS, wifiChannel, wifiBssid);
    ok = waitWifi(min<uint32_t>(4000, timeoutMs));
    if (!ok) { WiFi.disconnect(false, true); wifiChannel = 0; delay(20); }
  }
  if (!ok) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t spent = millis() - t0;
    ok = waitWifi(spent < timeoutMs ? timeoutMs - spent : 0);
  }
  if (ok) {
    memcpy(wifiBssid, WiFi.BSSID(), 6);
    wifiChannel = WiFi.channel();
    Serial.printf("[wifi] OK %lu ms, kanal %ld, RSSI=%d dBm, IP=%s\n",
                  (unsigned long)(millis() - t0), (long)wifiChannel, (int)WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] BLAD polaczenia");
  }
  return ok;
}

// Czy zegar systemowy zostal kiedykolwiek ustawiony (a nie stoi na 1970 r.)
static bool timeIsSet() {
  time_t n;
  time(&n);
  return n > 1700000000L;   // ~2023 r.
}

// dni od epoki dla daty kalendarzowej (algorytm Howarda Hinnanta)
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

// Zrodlo czasu: naglowek "Date" odpowiedzi HTTP. Sieci IoT czesto blokuja NTP
// (UDP 123), przepuszczajac zwykly ruch HTTP.
static bool setClockFromHttpDate(const String &d) {
  static const char *MON3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  int i = d.indexOf(", ");                       // "Tue, 08 Sep 2026 20:30:12 GMT"
  if (i < 0) return false;
  int day, year, hh, mm, ss;
  char mon[4] = { 0 };
  if (sscanf(d.c_str() + i + 2, "%d %3s %d %d:%d:%d",
             &day, mon, &year, &hh, &mm, &ss) != 6) return false;
  int m = -1;
  for (int k = 0; k < 12; k++)
    if (strncmp(mon, MON3[k], 3) == 0) { m = k + 1; break; }
  if (m < 0) return false;

  time_t epoch = (time_t)(daysFromCivil(year, m, day) * 86400LL + hh * 3600 + mm * 60 + ss);
  if (epoch < 1700000000L) return false;
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  Serial.printf("[czas] ustawiony z naglowka HTTP Date: %s\n", d.c_str());
  return true;
}

static bool httpGetJson(const String &url, JsonDocument &doc, const char *tag,
                        bool setClock = false) {
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  if (!http.begin(url)) { Serial.printf("[%s] http.begin() nieudane\n", tag); return false; }
  static const char *WANTED[] = { "Date" };
  http.collectHeaders(WANTED, 1);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[%s] HTTP %d\n", tag, code);
    http.end();
    return false;
  }
  if (setClock) setClockFromHttpDate(http.header("Date"));
  // Open-Meteo odpowiada w trybie chunked; getString() sklada tresc poprawnie,
  // a getStream() oddalby parserowi naglowki rozmiaru kawalkow.
  String payload = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[%s] JSON: %s (%u B)\n", tag, err.c_str(), (unsigned)payload.length());
    return false;
  }
  return true;
}

static bool fetchWeather() {
  if (!connectWifi(10000)) return false;
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=") + LOC_LAT + "&longitude=" + LOC_LON +
      "&current=temperature_2m,apparent_temperature,weather_code,is_day"
      "&hourly=temperature_2m,precipitation,precipitation_probability,weather_code,wind_speed_10m,wind_gusts_10m,visibility"
      "&forecast_hours=49&timezone=Europe%2FWarsaw&timeformat=unixtime";
  JsonDocument doc;
  if (!httpGetJson(url, doc, "pogoda", true)) return false;
  static Weather next;   // 2 kB: poza stosem zadania, ktory dzieli z HTTP i Wi-Fi
  if (!timeIsSet() || !decodeWeather(doc.as<JsonVariantConst>(), time(nullptr), next)) {
    Serial.println("[pogoda] odrzucono niespojna lub nieaktualna os czasu");
    return false;
  }
  wx = next;
  char summary[180];
  bool urgent;
  makeSummary(time(nullptr), summary, sizeof(summary), urgent);
  Serial.printf("[pogoda] OK %d godzin, %.1f C, odczuwalna %.1f C, kod %d\n[pasek] %s\n",
                wx.n, wx.temp, wx.feels, wx.code, summary);
  return true;
}

static bool fetchNearRain() {
  time_t now = time(nullptr);
  if (!*TOMORROW_API_KEY || !timeIsSet() || WiFi.status() != WL_CONNECTED || now < nearRainRetryAt) return false;
  if (!nearRainUseful(now, batteryLow())) {
    Serial.println("[opady-blisko] pominieto: brak sygnalu opadow w prognozie na 3 h");
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(TOMORROW_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  String url = String("https://api.tomorrow.io/v4/timelines?location=") + LOC_LAT + "," + LOC_LON +
      "&fields=precipitationIntensity,precipitationType&timesteps=5m&units=metric&startTime=now&endTime=nowPlus2h";
  if (!http.begin(client, url)) return false;
  http.addHeader("apikey", TOMORROW_API_KEY);   // klucz w naglowku, nie w adresie URL
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[opady-blisko] HTTP %d\n", code);
    if (code == 429) nearRainRetryAt = time(nullptr) + 3600;
    else if (code == 401 || code == 403) nearRainRetryAt = time(nullptr) + 21600;
    http.end();
    return false;
  }
  if (http.getSize() > 16384) { http.end(); return false; }
  String body = http.getString();
  http.end();
  if (body.length() > 16384) return false;
  JsonDocument doc;
  NearRain next;
  if (deserializeJson(doc, body) || !decodeNearRain(doc.as<JsonVariantConst>(), time(nullptr), next)) {
    Serial.println("[opady-blisko] odrzucono niepelne lub nieaktualne dane");
    return false;
  }
  nearRain = next;
  nearRainRetryAt = 0;
  char message[180];
  bool freezing;
  bool wet = nearRainMessage(time(nullptr), message, sizeof(message), freezing);
  Serial.printf("[opady-blisko] OK %d punktow / 5 min; %s\n", nearRain.n,
                wet ? message : "brak sygnalu opadow, pozostaje podsumowanie dnia");
  return true;
}

// ===========================================================================
//  BATERIA
// ===========================================================================
static void measureBattery() {
  analogReadResolution(12);
  analogSetPinAttenuation(9, ADC_11db);
  analogReadMilliVolts(9); delay(5);   // odrzuc pierwsza konwersje
  uint32_t samples[65];
  for (int i = 0; i < 65; i++) { samples[i] = analogReadMilliVolts(9); delay(2); }
  for (int i = 1; i < 65; i++) {
    uint32_t value = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > value) { samples[j + 1] = samples[j]; j--; }
    samples[j + 1] = value;
  }
  uint32_t sum = 0;
  for (int i = 16; i < 49; i++) sum += samples[i];
  float voltage = (sum / 33.f) * (2.3f / 1.3f) / 1000.f * BATTERY_CAL;
  acceptBatteryVoltage(voltage);
  BatteryLogEntry &e = batteryLog[batteryLogHead];
  e.at = timeIsSet() ? (uint32_t)time(nullptr) : 0;
  e.pinMv = (uint16_t)(sum / 33);
  e.batMv = isfinite(voltage) && voltage > 0 && voltage < 65 ? (uint16_t)lroundf(voltage * 1000) : 0;
  batteryLogHead = (batteryLogHead + 1) % BATTERY_LOG_SIZE;
  if (batteryLogCount < BATTERY_LOG_SIZE) batteryLogCount++;
  Serial.printf("[bateria] ADC=%lu..%lu mV, pomiar=%.3f V, szacunek=%d%%\n",
                (unsigned long)samples[0], (unsigned long)samples[64], voltage, batteryPercent);
}

static void printBatteryLog() {
  Serial.printf("[bateria-log] %d pomiarow (najstarszy pierwszy), V po korekcie x%.3f\n",
                batteryLogCount, BATTERY_CAL);
  for (int i = 0; i < batteryLogCount; i++) {
    const BatteryLogEntry &e = batteryLog[(batteryLogHead + BATTERY_LOG_SIZE - batteryLogCount + i) % BATTERY_LOG_SIZE];
    char when[20] = "bez czasu";
    if (e.at) { time_t t = e.at; tm lt; localtime_r(&t, &lt); strftime(when, sizeof(when), "%d.%m %H:%M", &lt); }
    Serial.printf("[bateria-log] %s  ADC=%u mV  bateria=%u.%03u V  %d%%\n", when, e.pinMv,
                  e.batMv / 1000, e.batMv % 1000, (int)lroundf(batterySoc(e.batMv / 1000.f)));
  }
}

// Krytycznie niskie napiecie: bez Wi-Fi (skoki pradu radia moga wywolac
// brownout i petle restartow) i bez zegara. Wyjscie z histereza.
static bool updateCriticalMode() {
  if (isfinite(batteryVoltage)) {
    if (batteryVoltage < BATTERY_CRITICAL_V) criticalMode = true;
    else if (batteryVoltage >= BATTERY_RESUME_V) criticalMode = false;
  }
  return criticalMode;
}

// ===========================================================================
//  EKRAN I SEN
// ===========================================================================
static void renderFull(time_t now) {
  for (int k = 0; k < 4; k++) {
    time_t start = now - now % 3600 + k * 10800;
    RainBlock r = rainBlock(start);
    char label[64];
    screenRainBlockText(r, k, now, label, sizeof(label));
    Serial.printf("[opad3h] %lld..%lld suma=%.4f popMax=%.1f complete=%d/%d black=%d tekst=%s\n",
                  (long long)start, (long long)(start + 10800), r.amount, r.pop,
                  r.amountKnown, r.popKnown, rainBlockBlack(r), label);
  }
  pushFull();
}

static const int retainedPins[] = { EPD_POWER, EPD_CS, EPD_DC, EPD_RST, EPD_SCK, EPD_MOSI };

// Czekanie na BUSY panelu (aktywny stan niski) w light sleep. Budzi zbocze
// konca odswiezenia na GPIO38; timer jest tylko zabezpieczeniem.
static void waitPanelSleeping(const void *) {
  static bool armed = false;
  if (!armed) {
    gpio_wakeup_enable((gpio_num_t)EPD_BUSY, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    armed = true;
  }
  esp_sleep_enable_timer_wakeup(200000);
  esp_light_sleep_start();
}

static void deepSleep(uint64_t sleepUs) {
  gpio_wakeup_disable((gpio_num_t)EPD_BUSY);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  display.powerOff();   // wylacz wysokie napiecia, zachowaj RAM kontrolera
  WiFi.mode(WIFI_OFF);
  memcpy(weatherCache, &wx, sizeof(wx));
  memcpy(nearRainCache, &nearRain, sizeof(nearRain));
  savedBatteryVoltage = batteryVoltage;
  savedBatteryPercent = batteryPercent;
  cacheMagic = CACHE_MAGIC;
  SPI.end();
  // Stabilne sygnaly podczas resetu CPU po deep sleep, bez resetu panelu.
  for (int pin : retainedPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (pin == EPD_SCK || pin == EPD_MOSI) ? LOW : HIGH);
    gpio_hold_en((gpio_num_t)pin);
  }
  gpio_deep_sleep_hold_en();
  Serial.printf("[sen] aktywny=%lu ms, sen=%llu ms, WiFi=OFF\n",
                (unsigned long)millis(), sleepUs / 1000);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

static uint64_t usUntilNextMinute() {
  if (!timeIsSet()) {
    // Bez czasu: ponowienie z rosnaca przerwa (5, 10, 20, 30 min).
    return (uint64_t)retryDelaySeconds(fetchFailures, 300, WEATHER_MAX_BACKOFF_MIN * 60) * 1000000ULL;
  }
  timeval tv;
  gettimeofday(&tv, nullptr);
  // 50 ms zapasu: wybudzenie tuz przed pelna minuta pokazaloby stara minute.
  return (60 - tv.tv_sec % 60) * 1000000ULL - tv.tv_usec + 50000ULL;
}

// Ostatnia linia obrony: zawieszony cykl (siec, TLS, sterownik) nie moze
// trzymac procesora wlaczonego do rozladowania akumulatora.
static void cycleWatchdog(void *) {
  cacheMagic = 0;   // nastepny start od zera, z pelna inicjalizacja panelu
  esp_sleep_enable_timer_wakeup(300000000ULL);
  esp_deep_sleep_start();
}

static void armCycleWatchdog() {
  static esp_timer_handle_t timer = nullptr;
  esp_timer_create_args_t args = {};
  args.callback = cycleWatchdog;
  args.name = "cykl";
  if (esp_timer_create(&args, &timer) == ESP_OK)
    esp_timer_start_once(timer, (uint64_t)CYCLE_WATCHDOG_S * 1000000ULL);
}

static void drawCriticalScreen() {
  fb.fillScreen(PAPER);
  textColor = INK;
  drawString(400, 200, "Naładuj baterię", SUMMARY, CENTER);
  char b[64];
  snprintf(b, sizeof(b), "Napięcie %.2f V. Pogoda i zegar wstrzymane", batteryVoltage);
  for (char *p = b; *p; p++) if (*p == '.') { *p = ','; break; }
  drawString(400, 250, b, DESC, CENTER);
  drawString(400, 290, "Wrócą same po podłączeniu ładowarki", DESC, CENTER);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  armCycleWatchdog();
  bool resume = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && cacheMagic == CACHE_MAGIC;
  Serial.printf("\n=== C Studio 2.0 / ESPink BAT / %s ===\n", resume ? "wybudzenie" : "start");
  setenv("TZ", TZ_INFO, 1);
  tzset();
  if (resume) {
    memcpy(&wx, weatherCache, sizeof(wx));
    memcpy(&nearRain, nearRainCache, sizeof(nearRain));
    batteryVoltage = savedBatteryVoltage;
    batteryPercent = savedBatteryPercent;
  } else {
    lastAttempt = nextFetchAt = lastScreenHour = lastCompose = 0;
    fetchFailures = 0;
    lastScreenStale = true;
    contentValid = false;
    lastFullRefresh = 0;
    renderedBatteryPercent = -1;
    nearRainRetryAt = 0;
    criticalMode = criticalShown = false;
    batteryLogCount = batteryLogHead = 0;
    wifiChannel = 0;
  }
  // Ustaw poziomy PRZED zwolnieniem zatrzaskow (bez impulsu resetu / zaniku zasilania).
  for (int pin : retainedPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (pin == EPD_SCK || pin == EPD_MOSI) ? LOW : HIGH);
    gpio_hold_dis((gpio_num_t)pin);
  }
  gpio_deep_sleep_hold_dis();
  if (!resume) delay(500);
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.epd2.preserveReset(resume);
  display.init(SERIAL_BAUD, !resume, 10, false, SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.epd2.preserveReset(false);
  display.epd2.setBusyCallback(waitPanelSleeping);
  display.setRotation(0);

  time_t now = time(nullptr);
  bool due = weatherDue(resume, timeIsSet(), now, lastAttempt, nextFetchAt);
  // Pomiar przed Wi-Fi i odswiezaniem: po starcie, przy kazdym pobraniu pogody
  // (co 15 min, ok. 0,13 s) i w trybie krytycznym. Wskaznik nadaza za ladowaniem.
  if (!resume || criticalMode || due) {
    measureBattery();
    printBatteryLog();
  }
  if (updateCriticalMode()) {
    Serial.printf("[bateria] krytycznie niskie napiecie %.3f V: bez Wi-Fi i zegara\n", batteryVoltage);
    if (!criticalShown) {
      drawCriticalScreen();
      pushFull();
      criticalShown = true;
      contentValid = false;   // po naladowaniu wracamy pelnym odswiezeniem
    }
    deepSleep((uint64_t)CRITICAL_SLEEP_MIN * 60000000ULL);
  }
  criticalShown = false;

  if (due) {
    Serial.println("[cykl] pobieranie pogody");
    bool ok = fetchWeather();
    fetchNearRain();
    WiFi.mode(WIFI_OFF);
    delay(10);
    now = time(nullptr);
    lastAttempt = now;
    fetchFailures = ok ? 0 : (uint8_t)min(fetchFailures + 1, 20);
    nextFetchAt = nextFetchTime(now, fetchFailures, fetchIntervalSeconds(batteryLow()),
                                WEATHER_MAX_BACKOFF_MIN * 60);
    Serial.printf("[cykl] pobranie=%s, nastepne ok. %lld s\n",
                  ok ? "OK" : "BLAD, zachowano poprzednie dane", (long long)(nextFetchAt - now));
  }

  now = time(nullptr);
  tm ti;
  localtime_r(&now, &ti);
  if (timeIsSet()) {
    if (due || !resume || now / 3600 != lastScreenHour || stale(now) != lastScreenStale ||
        now - lastFullRefresh >= 3600 || nowcastRecompose(nearRainFresh(now), now, lastCompose)) {
      composeScreen(ti, now);
      uint64_t current = contentFingerprint(fb.getBuffer());
      bool wasLow = renderedBatteryPercent >= 0 && renderedBatteryPercent <= 5;
      if (wasLow != batteryLow() ||
          fullRefreshNeeded(resume, contentValid, previousContent, current, now, lastFullRefresh)) {
        Serial.println("[ekran] pelny: zmiana tresci, start lub czyszczenie");
        renderFull(now);
        previousContent = current;
        contentValid = true;
        lastFullRefresh = now;
      } else {
        // Jedno odswiezenie czesciowe obejmuje zegar i nowa godzine aktualizacji.
        pushWindow(16, 0, 768, 164, false);
        if (renderedBatteryPercent != batteryPercent && !batteryLow())
          pushWindow(720, 462, 80, 18, false);
        Serial.println("[ekran] dane bez zmian: aktualizacja czesciowa");
      }
      renderedBatteryPercent = batteryPercent;
      lastCompose = now;
      lastScreenHour = now / 3600;
      lastScreenStale = stale(now);
    } else {
      drawClock(ti);
      pushWindow(16, 50, 336, 112, false);
      Serial.printf("[zegar] %02d:%02d, bez WiFi\n", ti.tm_hour, ti.tm_min);
    }
  } else if (!resume) {
    fb.fillScreen(PAPER);
    drawString(400, 222, "Brak połączenia z pogodą", SUMMARY_S, CENTER);
    char b[64];
    snprintf(b, sizeof(b), "Ponowna próba za %d min",
             retryDelaySeconds(fetchFailures, 300, WEATHER_MAX_BACKOFF_MIN * 60) / 60);
    drawString(400, 270, b, DESC, CENTER);
    pushFull();
  }
  deepSleep(usUntilNextMinute());
}

void loop() {}
