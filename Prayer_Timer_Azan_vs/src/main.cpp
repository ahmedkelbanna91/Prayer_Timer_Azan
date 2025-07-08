/*
 * pin 1 - D2  - not used    |  Micro SD card     |
 * pin 2 - D3  - CS (SS)     |                   /
 * pin 3 - CMD - DI (MOSI)   |                  |__
 * pin 4 - VDD - VDD (3.3V)  |                    |
 * pin 5 - CLK - SCK (SCLK)  | 8 7 6 5 4 3 2 1   /
 * pin 6 - VSS - VSS (GND)   | ▄ ▄ ▄ ▄ ▄ ▄ ▄ ▄  /
 * pin 7 - D0  - DO (MISO)   | ▀ ▀ █ ▀ █ ▀ ▀ ▀ |
 * pin 8 - D1  - not used    |_________________|
 *                     ╔═══════╝ ║ ║ ║ ║ ║ ║ ╚════╗
 *                     ║         ║ ║ ║ ║ ║ ╚══╗   ║
 *                     ║   ╔═════╝ ║ ║ ║ ╚═╗  ║   ║
 * Connections for     ║   ║   ╔═══╝ ║ ║   ║  ║   ║
 *                     ║   ║   ║   ╔═╝ ║   ║  ║   ║
 * SD card             ║   ║   ║   ║   ║   ║  ║   ║
 * ESP32            | D1  D0  GND CLK 3V3 CMD D3  D2 
 * MMC              |  4   2       14      15 13  12 
 *                  | PU  PU                  PU  PU
 *                  *********************************
 * ESP32            |  -  DO  GND SCK 3V3  DI CS   - 
 * SD               |  -  19       18      23 17   -

 * WARNING: ALL data pins must be pulled up to 3.3V with an external 10k Ohm resistor!
 * Note to ESP32 pin 2 (D0): Add a 1K Ohm pull-up resistor to 3.3V after flashing
 *
*/

#define USE_PSRAM true
#define SET_LOOP_TASK_STACK_SIZE (16 * 1024)  // 16 KB

#include <Arduino.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "SD_MMC.h"
#include "FS.h"
#include "time.h"
#include "esp_sntp.h"
#include <Adafruit_NeoPixel.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "Audio.h" 
#include "webPage.h"

// =================================================
// No FS 4MB (2MB APP x2)

#define LED_PIN 5  // Digital pin connected to the SK6812 data line

#define SD_TYPE SD_MMC
#define MMC_D1 4
#define MMC_D0 2
#define MMC_CLK 14
#define MMC_CMD 15
#define MMC_D3 13
#define MMC_D2 12

//I2S MAX98357
#define I2S_LRC 25
#define I2S_BCLK 26
#define I2S_DOUT 27
#define I2S_SD 33

const char *SSID = "azanhub";
const char *PASS = "12345678";


const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

const char *time_zone = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
struct tm timeinfo;

char Time_Fajr[10], Time_Dhuhr[10], Time_Asr[10], Time_Maghrib[10], Time_Isha[10];
char Date_hijri[20], Month_hijri[20], Date_Gregorian[20], Weekday_Gregorian[20], PrayerData[100];
bool got_time = false;
bool prayerTimesFetched = false;
bool OTA = false, playSurahSequence = false;

int TimeInMinsTEST = -1, lastMinute = -1;

const char *tracks[] = {
  "Fajr.mp3",       //0
  "Dhuhr.mp3",      //1
  "Asr.mp3",        //2
  "Maghrib.mp3",    //3
  "Isha.mp3",       //4
  "Azan-N.mp3",     //5
  "Azan.mp3",       //6
  "Al-Fatiha.mp3",  //7
  "Al-Ikhlas.mp3",  //8
  "Al-Falaq.mp3",   //9
  "An-Nas.mp3",     //10
  "Al-Kahf.mp3",    //11
};

// Struct to store prayer times
struct Prayers {
  const char *name;
  int vol;
  int inMinutes;
};

Prayers prayerTimes[5];


WiFiManager wifiManager;  // global wm instance
Adafruit_NeoPixel rgbwLED(1, LED_PIN, NEO_GRBW + NEO_KHZ800);
AsyncWebServer server(80);
Audio audio;

void debugPrint(const char *format, ...);
void setup();
void loop();
void checkPrayerTimes(int tim);
void fetchData(int day, int month, int year, const char *city, const char *country);
void configModeCallback(WiFiManager *myWiFiManager);
void esp_wifi_stp();
void esp_ota_stp();
const char *formatBytes(size_t bytes);
const char *listDir(const char *dirname, uint8_t levels, bool print);
void esp_wfm_stp();
void esp_mdns_stp();
void timeavailable(struct timeval *t);
void esp_ntp_stp();
void performUpdate(Stream &updateSource, size_t updateSize);
void esp_sd_ota_stp();
void esp_audio_stp();
void AudioVU();
void audio_eof_mp3(const char *info);
void playAudio(const char *filename, int volume);
void playSequence(int vol, int repeatCount);
void esp_neo_stp();
void SHOW_LED(int R, int G, int B, int W);
void esp_sd_stp();

#define _PRINT_DEBUG_(...) debugPrint(__VA_ARGS__)

void setup() {
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, LOW);

  vTaskDelay(5000 / portTICK_PERIOD_MS);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  if (psramInit()) _PRINT_DEBUG_("✅   PSRAM Detected! Total: %d bytes | Free: %d bytes\n", ESP.getPsramSize(), ESP.getFreePsram());

  esp_neo_stp();
  esp_sd_stp();
  esp_sd_ota_stp();
  esp_wifi_stp();
  esp_ntp_stp();
  esp_audio_stp();
  esp_ota_stp();
  esp_wfm_stp();
  esp_mdns_stp();

  // ⚠️   text
  // ✅   text
  // ❌   text
}

void loop() {
  ArduinoOTA.handle();
  delay(10);

  if (!OTA) {
    AudioVU();
    audio.loop();
    digitalWrite(I2S_SD, audio.isRunning() ? HIGH : LOW);

    if (!prayerTimesFetched) {
      fetchData(timeinfo.tm_mday - 1, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, "Dusseldorf", "Germany");
    } else {
      static unsigned long lastTimeCheck = 0;
      if (millis() - lastTimeCheck >= 1000) {
        lastTimeCheck = millis();

        static int lastAttemptHour = -1;
        if ((timeinfo.tm_hour % 12 == 0) && (timeinfo.tm_hour != lastAttemptHour)) {
          fetchData(timeinfo.tm_mday - 1, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, "Dusseldorf", "Germany");
          lastAttemptHour = timeinfo.tm_hour;
          _PRINT_DEBUG_("✅   [HTTP] Fetching Data for checkup [%d]\n", timeinfo.tm_hour);
        }

        if (!getLocalTime(&timeinfo)) {
          got_time = false;
          _PRINT_DEBUG_("❌   Failed to obtain time\n");
        } else {
          got_time = true;
          if (!audio.isRunning()) SHOW_LED(0, 5, 0, 0);
          checkPrayerTimes(timeinfo.tm_hour * 60 + timeinfo.tm_min);

          // debugging();

          _PRINT_DEBUG_("%02d:%02d:%02d  %02d.%02d.%04d   %s-%s  %s-%s  F%s D%s A%s M%s I%s\n%s\n",
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                        timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                        Date_Gregorian, Weekday_Gregorian,
                        Date_hijri, Month_hijri, Time_Fajr, Time_Dhuhr,
                        Time_Asr, Time_Maghrib, Time_Isha, PrayerData);
        }
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    _PRINT_DEBUG_("❌   WiFi disconnected. Restarting...\n");
    SHOW_LED(255, 0, 0, 0);                 // Red LED to indicate disconnection
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // Wait 5 seconds before retrying
    ESP.restart();
    return;
  }
}


void debugPrint(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, args);
  Serial.print(buffer);
  va_end(args);
}

void debugging() {
  if (Serial.available()) {
    int input = Serial.parseInt();
    if (input > 0) {
      TimeInMinsTEST = input;
    }
  }
  if (timeinfo.tm_min != lastMinute) {
    lastMinute = timeinfo.tm_min;
    TimeInMinsTEST = TimeInMinsTEST + 1;
    _PRINT_DEBUG_("TimeInMinsTEST: %d\n", TimeInMinsTEST);
  }
  checkPrayerTimes(TimeInMinsTEST);
  int TimeInMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  _PRINT_DEBUG_("\n%d  %d   F%d D%d A%d M%d I%d\n",
                TimeInMins, TimeInMinsTEST,
                prayerTimes[0].inMinutes, prayerTimes[1].inMinutes, prayerTimes[2].inMinutes,
                prayerTimes[3].inMinutes, prayerTimes[4].inMinutes);
}

void checkPrayerTimes(int tim) {
  if (!got_time) return;
  if (!prayerTimesFetched) return;

  for (size_t i = 0; i < 5; i++) {
    int hrs = (prayerTimes[i].inMinutes - tim) / 60;
    int mins = (prayerTimes[i].inMinutes - tim) % 60;

    if (tim == prayerTimes[i].inMinutes) {
      playAudio(tracks[6], prayerTimes[i].vol);
      snprintf(PrayerData, sizeof(PrayerData), "It's time for %s", prayerTimes[i].name);
      break;
    } else if (tim == prayerTimes[i].inMinutes - 1) {
      playAudio(tracks[i], prayerTimes[i].vol);
      snprintf(PrayerData, sizeof(PrayerData), "1 minute until %s", prayerTimes[i].name);
      break;
    } else if (tim < prayerTimes[i].inMinutes && hrs == 0) {
      snprintf(PrayerData, sizeof(PrayerData), "Next prayer is %s in %d min", prayerTimes[i].name, mins);
      break;
    }
  }

  int set_tim = 8 * 60;  //480
  if (tim == set_tim) {
    playSurahSequence = true;
  }
  playSequence(5, 2);

  if (tim > prayerTimes[4].inMinutes && !audio.isRunning()) {
    PrayerData[0] = '\0';  // Clear PrayerData
  }
}


void fetchData(int day, int month, int year, const char *city, const char *country) {
  if (WiFi.status() != WL_CONNECTED) {
    _PRINT_DEBUG_("❌   [WiFi] Not connected, skipping fetchData\n");
    return;
  }
  if (!got_time) return;

  //"http://api.aladhan.com/v1/calendarByCity/2025/3?city=Dusseldorf&country=Germany&method=3"
  char url[256];
  snprintf(url, sizeof(url), "http://api.aladhan.com/v1/calendarByCity/%d/%d?city=%s&country=%s&method=3",
           year, month, city, country);

  _PRINT_DEBUG_("✅   [HTTP] fetchData begin...\n✅   %s\n", url);

  HTTPClient http;
  // http.useHTTP10(true);
  http.setTimeout(30000);  // 10 seconds timeout

  if (!http.begin(url)) {
    _PRINT_DEBUG_("❌   [HTTP] Failed to initialize HTTP request\n");
    prayerTimesFetched = false;
    SHOW_LED(255, 0, 0, 0);
    return;
  }

  int httpCode = http.GET();
  _PRINT_DEBUG_("✅   [HTTP] fetchData GET... code: %d\n", httpCode);
  if (httpCode == 429) {  // Too Many Requests
    _PRINT_DEBUG_("❌   [HTTP] API rate limit reached. Waiting before retrying...\n");
    vTaskDelay(60000 / portTICK_PERIOD_MS);  // Wait 1 minute
  }

  if (httpCode != 200) {
    prayerTimesFetched = false;
    _PRINT_DEBUG_("❌   [HTTP] Request failed: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    SHOW_LED(255, 0, 0, 0);
    return;
  }

  JsonDocument filter;
  filter["data"][0]["timings"]["Fajr"] = true;
  filter["data"][0]["timings"]["Dhuhr"] = true;
  filter["data"][0]["timings"]["Asr"] = true;
  filter["data"][0]["timings"]["Maghrib"] = true;
  filter["data"][0]["timings"]["Isha"] = true;
  filter["data"][0]["date"]["gregorian"]["date"] = true;
  filter["data"][0]["date"]["gregorian"]["weekday"]["en"] = true;
  filter["data"][0]["date"]["hijri"]["date"] = true;
  filter["data"][0]["date"]["hijri"]["month"]["en"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

  http.end();

  if (error) {
    prayerTimesFetched = false;
    _PRINT_DEBUG_("❌   [JSON] Parsing failed: %s\n", error.c_str());
    SHOW_LED(255, 0, 0, 0);
    return;
  }

  JsonObject dataObject = doc["data"][day];
  if (dataObject.isNull()) {
    _PRINT_DEBUG_("❌   [JSON] Data object is empty\n");
    SHOW_LED(255, 0, 0, 0);
    return;
  }

  auto getTimeFromString = [](const char *timeStr, char *output, size_t outputSize) {
    const char *space = strchr(timeStr, ' ');  // Find the first space character
    if (space) {
      size_t length = space - timeStr;   // Calculate the length of the substring
      strncpy(output, timeStr, length);  // Copy the substring
      output[length] = '\0';             // Null-terminate the output
    } else {
      strncpy(output, timeStr, outputSize);  // If no space, copy the entire string
    }
  };

  // Convert prayer times to minutes
  auto time2Minutes = [](const char *timeStr) -> int {
    int hour, minute;
    sscanf(timeStr, "%d:%d", &hour, &minute);
    return hour * 60 + minute;
  };

  // Extract prayer times and dates

#define COPY_JSON_STRING(dest, src) \
  strncpy(dest, src, sizeof(dest)); \
  dest[sizeof(dest) - 1] = '\0';  // Ensure null-termination

  // Extract JSON values
  const char *fajr = dataObject["timings"]["Fajr"].as<const char *>();
  const char *dhuhr = dataObject["timings"]["Dhuhr"].as<const char *>();
  const char *asr = dataObject["timings"]["Asr"].as<const char *>();
  const char *maghrib = dataObject["timings"]["Maghrib"].as<const char *>();
  const char *isha = dataObject["timings"]["Isha"].as<const char *>();
  const char *d_hijri = dataObject["date"]["hijri"]["date"].as<const char *>();
  const char *m_hijri = dataObject["date"]["hijri"]["month"]["en"].as<const char *>();
  const char *d_gregorian = dataObject["date"]["gregorian"]["date"].as<const char *>();
  const char *w_gregorian = dataObject["date"]["gregorian"]["weekday"]["en"].as<const char *>();

  // Copy JSON values into buffers
  getTimeFromString(fajr, Time_Fajr, sizeof(Time_Fajr));
  getTimeFromString(dhuhr, Time_Dhuhr, sizeof(Time_Dhuhr));
  getTimeFromString(asr, Time_Asr, sizeof(Time_Asr));
  getTimeFromString(maghrib, Time_Maghrib, sizeof(Time_Maghrib));
  getTimeFromString(isha, Time_Isha, sizeof(Time_Isha));

  COPY_JSON_STRING(Date_hijri, d_hijri);
  COPY_JSON_STRING(Month_hijri, m_hijri);
  COPY_JSON_STRING(Date_Gregorian, d_gregorian);
  COPY_JSON_STRING(Weekday_Gregorian, w_gregorian);


  prayerTimes[0] = { "Fajr", 5, time2Minutes(Time_Fajr) };
  prayerTimes[1] = { "Dhuhr", 8, time2Minutes(Time_Dhuhr) };
  prayerTimes[2] = { "Asr", 8, time2Minutes(Time_Asr) };
  prayerTimes[3] = { "Maghrib", 8, time2Minutes(Time_Maghrib) };
  prayerTimes[4] = { "Isha", 6, time2Minutes(Time_Isha) };

  prayerTimesFetched = true;
  // doc.clear();
  // dataObject.clear();
  // filter.clear();

  SHOW_LED(0, 255, 0, 0);
  _PRINT_DEBUG_("✅   Prayer times fetched successfully\n");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  _PRINT_DEBUG_("⚠️   Entered config mode\nIP: %s\nSSID: %s\n",
                WiFi.softAPIP().toString().c_str(),
                myWiFiManager->getConfigPortalSSID().c_str());
}

void esp_wifi_stp() {
  WiFi.setSleep(false);  // Disable WiFi sleep to avoid disconnection
  WiFi.mode(WIFI_STA);
  wifiManager.setClass("invert");
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(60);
  wifiManager.setAPClientCheck(true);  // avoid timeout if client connected to softap
  wifiManager.setScanDispPerc(true);   // show RSSI as percentage not graph icons
  wifiManager.setConnectRetries(3);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setHostname(SSID);

  if (!wifiManager.autoConnect(SSID, PASS)) {
    SHOW_LED(255, 0, 0, 0);
    _PRINT_DEBUG_("❌   failed to connect and hit timeout\n");
    for (int i = 0; i <= 9; i++) {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    ESP.restart();
  }
  SHOW_LED(0, 255, 0, 0);

  vTaskDelay(2000 / portTICK_PERIOD_MS);
  SHOW_LED(0, 0, 0, 0);
}

void esp_ota_stp() {
  ArduinoOTA.setHostname(SSID);
  ArduinoOTA.setPassword(PASS);
  ArduinoOTA.setTimeout(120000);

  ArduinoOTA.onStart([]() {
    _PRINT_DEBUG_("🛠️   Free Heap Before OTA: %d bytes\n", ESP.getFreeHeap());
    SHOW_LED(255, 0, 0, 0);
    audio.stopSong();
    SD_TYPE.end();
    server.end();
    MDNS.end();
    OTA = true;
    _PRINT_DEBUG_("🛠️   Free Heap Before OTA: %d bytes\n", ESP.getFreeHeap());
    _PRINT_DEBUG_("✅   Start updating %s\n", ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int prog = (progress * 100) / total;
    SHOW_LED(map(prog, 0, 100, 255, 0), map(prog, 0, 100, 0, 255), 0, 0);
    _PRINT_DEBUG_("🛠️   %u%%\n", prog);
  });
  ArduinoOTA.onEnd([]() {
    _PRINT_DEBUG_("🛠️   Free Heap Before OTA: %d bytes\n", ESP.getFreeHeap());
    _PRINT_DEBUG_("\n✅   OTA End\n✅   Restarting\n");
    SHOW_LED(0, 255, 0, 0);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP.restart();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    OTA = false;
    _PRINT_DEBUG_("❌   Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      _PRINT_DEBUG_("Auth Failed\n");
    } else if (error == OTA_BEGIN_ERROR) {
      _PRINT_DEBUG_("Begin Failed\n");
    } else if (error == OTA_CONNECT_ERROR) {
      _PRINT_DEBUG_("Connect Failed\n");
    } else if (error == OTA_RECEIVE_ERROR) {
      _PRINT_DEBUG_("Receive Failed\n");
    } else if (error == OTA_END_ERROR) {
      _PRINT_DEBUG_("End Failed\n");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();  // Restart to recover
  });
  ArduinoOTA.begin();
  _PRINT_DEBUG_("✅   OTA Ready\n");
}

const char *formatBytes(size_t bytes) {
  static char buffer[20];
  if (bytes < 1024) {
    snprintf(buffer, sizeof(buffer), "%zuB", bytes);
  } else if (bytes < (1024 * 1024)) {
    snprintf(buffer, sizeof(buffer), "%.2fKB", bytes / 1024.0);
  } else if (bytes < (1024 * 1024 * 1024)) {
    snprintf(buffer, sizeof(buffer), "%.2fMB", bytes / 1024.0 / 1024.0);
  } else {
    snprintf(buffer, sizeof(buffer), "%.2fGB", bytes / 1024.0 / 1024.0 / 1024.0);
  }
  return buffer;
}


const char *listDir(const char *dirname, uint8_t levels, bool print) {
  static char jsonResponse[2048];  // Static buffer to hold the JSON response

  if (print) _PRINT_DEBUG_("Listing directory: %s\n", dirname);

  File root = SD_TYPE.open(dirname);
  if (!root || !root.isDirectory()) {
    _PRINT_DEBUG_("Failed to open directory: %s\n", dirname);
    snprintf(jsonResponse, sizeof(jsonResponse), "{\"status\":\"error\",\"message\":\"Invalid directory\"}");
    return jsonResponse;
  }

  strncpy(jsonResponse, "[", sizeof(jsonResponse));

  File file = root.openNextFile();
  while (file) {
    if (strlen(jsonResponse) > 1) {
      strncat(jsonResponse, ",", sizeof(jsonResponse) - strlen(jsonResponse) - 1);
    }

    char entry[256];
    if (file.isDirectory()) {
      snprintf(entry, sizeof(entry), "{\"type\":\"folder\",\"name\":\"%s\"}", file.name());
      if (print) _PRINT_DEBUG_("DIR : %s\n", file.name());
    } else {
      snprintf(entry, sizeof(entry), "{\"type\":\"file\",\"name\":\"%s\",\"size\":\"%s\"}", file.name(), formatBytes(file.size()));
      if (print) _PRINT_DEBUG_("FILE: %s\tSIZE: %s\n", file.name(), formatBytes(file.size()));
    }

    strncat(jsonResponse, entry, sizeof(jsonResponse) - strlen(jsonResponse) - 1);
    file = root.openNextFile();
  }
  strncat(jsonResponse, "]", sizeof(jsonResponse) - strlen(jsonResponse) - 1);

  return jsonResponse;
}


void esp_wfm_stp() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html, index_html_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/gettimes", HTTP_GET, [](AsyncWebServerRequest *request) {
    auto convertTo12Hour = [](const char *timeStr, char *output, size_t outputSize) {
      int hour, minute;
      sscanf(timeStr, "%d:%d", &hour, &minute);         // Parse hour and minute from the time
      const char *period = (hour >= 12) ? "PM" : "AM";  // Determine AM/PM
      hour = (hour % 12 == 0) ? 12 : hour % 12;         // Convert 0 to 12 for 12-hour format
      snprintf(output, outputSize, "%02d:%02d %s", hour, minute, period);
    };

    char timeBuffer12[15];                                               // Buffer for 12-hour time (e.g., "02:30 PM")
    char timeBuffer24[10];                                               // Buffer for 24-hour time (e.g., "14:30:00")
    char fajr12[15], dhuhr12[15], asr12[15], maghrib12[15], isha12[15];  // Buffers for prayer times in 12-hour format
    char jsonResponse[512];                                              // Buffer for JSON response

    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;  // Convert 0 to 12 for 12-hour format
    snprintf(timeBuffer12, sizeof(timeBuffer12), "%02d:%02d:%02d %s", hour12, timeinfo.tm_min, timeinfo.tm_sec, (timeinfo.tm_hour >= 12) ? "PM" : "AM");
    snprintf(timeBuffer24, sizeof(timeBuffer24), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    convertTo12Hour(Time_Fajr, fajr12, sizeof(fajr12));
    convertTo12Hour(Time_Dhuhr, dhuhr12, sizeof(dhuhr12));
    convertTo12Hour(Time_Asr, asr12, sizeof(asr12));
    convertTo12Hour(Time_Maghrib, maghrib12, sizeof(maghrib12));
    convertTo12Hour(Time_Isha, isha12, sizeof(isha12));

    snprintf(jsonResponse, sizeof(jsonResponse),
             "{"
             "\"time24\":\"%s\","
             "\"time12\":\"%s\","
             "\"weekday\":\"%s\","
             "\"date\":\"%s\","
             "\"hijri_date\":\"%s   %s\","
             "\"Fajr\":\"%s\","
             "\"Dhuhr\":\"%s\","
             "\"Asr\":\"%s\","
             "\"Maghrib\":\"%s\","
             "\"Isha\":\"%s\","
             "\"prayerData\":\"%s\""
             "}",
             timeBuffer24, timeBuffer12,
             Weekday_Gregorian, Date_Gregorian,
             Date_hijri, Month_hijri,
             fajr12, dhuhr12, asr12, maghrib12, isha12,
             (PrayerData[0] == '\0') ? "No upcoming prayers today." : PrayerData);

    request->send(200, "application/json", jsonResponse);
  });

  server.on("/playquraanradio", HTTP_GET, [](AsyncWebServerRequest *request) {
    static bool isQuranRadioPlaying = false;
    if (isQuranRadioPlaying) {
      audio.stopSong();  // Stop playback if already playing
      isQuranRadioPlaying = false;
      _PRINT_DEBUG_("🔇 Quran Radio Stopped\n");
    } else {
      audio.setVolume(5);
      audio.connecttohost("https://n0a.radiojar.com/8s5u5tpdtwzuv?rj-ttl=5&rj-tok=AAABlZY1cvsAFpzmBzwQGeHhhw");
      isQuranRadioPlaying = true;
      _PRINT_DEBUG_("🔊 Quran Radio Playing\n");
    }
    request->send(200, "text/plain", isQuranRadioPlaying ? "Playing" : "Stopped");
  });

  server.on("/setvolume", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("level")) {
      int vol = request->getParam("level")->value().toInt();
      vol = constrain(vol, 0, 21);  // Ensure volume is within valid range (0-21)
      audio.setVolume(vol);         // Set the volume
      _PRINT_DEBUG_("🔊 Quran Radio Volume Set to: %d\n", vol);
      char response[50];  // Buffer for the response message
      snprintf(response, sizeof(response), "Volume set to %d", vol);
      request->send(200, "text/plain", response);
    } else {
      request->send(400, "text/plain", "Missing 'level' parameter");
    }
  });

  server.on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", file_html, file_html_len);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/getcontents", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Path parameter missing\"}");
      return;
    }

    const char *path = request->getParam("path")->value().c_str();
    if (strlen(path) == 0) path = "/";
    _PRINT_DEBUG_("Request folder contents for path: %s\n", path);
    request->send(200, "application/json", listDir(path, 0, false));
  });


  server.on(
    "/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"File upload complete\"}");
    },
    [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      char file_path[256];  // Buffer for the file path
      snprintf(file_path, sizeof(file_path), "/%s", filename.c_str());

      if (!index) {
        _PRINT_DEBUG_("UploadStart: %s\n", file_path);
        if (SD_TYPE.exists(file_path)) {
          SD_TYPE.remove(file_path);
        }
      }

      File file = SD_TYPE.open(file_path, FILE_APPEND);
      if (file) {
        file.write(data, len);
        file.close();
      }

      if (final) {
        _PRINT_DEBUG_("UploadEnd: %s, Total Size: %u B\n", file_path, index + len);
      }
    });

  // Delete file
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path") || !request->hasParam("type")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Path or type parameter missing\"}");
      return;
    }

    const char *path = request->getParam("path")->value().c_str();
    const char *type = request->getParam("type")->value().c_str();

    _PRINT_DEBUG_("Deleting %s: %s\n", type, path);

    if (strcmp(type, "folder") == 0) {
      File dir = SD_TYPE.open(path);
      if (dir && dir.isDirectory()) {
        while (File file = dir.openNextFile()) {
          SD_TYPE.remove(file.name());
        }
        dir.close();
        bool success = SD_TYPE.rmdir(path);
        if (success) {
          request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Folder deleted successfully\"}");
        } else {
          request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to delete folder\"}");
        }
      } else {
        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Folder not found\"}");
      }
    } else if (strcmp(type, "file") == 0) {
      if (SD_TYPE.exists(path)) {
        SD_TYPE.remove(path);
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"File deleted successfully\"}");
      } else {
        request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid type parameter\"}");
    }
  });

  // Download file
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Path parameter missing\"}");
      return;
    }

    const char *path = request->getParam("path")->value().c_str();
    _PRINT_DEBUG_("Downloading File: %s\n", path);

    if (SD_TYPE.exists(path)) {
      request->send(SD_TYPE, path, String(), true);
    } else {
      request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
    }
  });

  // Create NewFolder
  server.on("/createfolder", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Path parameter missing\"}");
      return;
    }

    const char *path = request->getParam("path")->value().c_str();
    _PRINT_DEBUG_("Creating Folder: %s\n", path);

    if (SD_TYPE.exists(path)) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Folder already exists\"}");
      return;
    }

    bool success = SD_TYPE.mkdir(path);
    if (success) {
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Folder created successfully\"}");
    } else {
      request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to create folder\"}");
    }
  });


  _PRINT_DEBUG_("✅   Web fileManager started\n");
  server.begin();
}


void esp_mdns_stp() {
  if (MDNS.begin(SSID)) {
    MDNS.addService("http", "tcp", 80);
    _PRINT_DEBUG_("✅   MDNS responder started\n");
    _PRINT_DEBUG_("✅   You can now connect to http://%s.local\n\n", SSID);
  }
}

// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t) {
  if (!getLocalTime(&timeinfo)) {
    _PRINT_DEBUG_("❌   No time available (yet)\n");
    return;
  }
  _PRINT_DEBUG_("✅   Got time adjustment from NTP!\n");
  _PRINT_DEBUG_("✅   %02d.%02d.%04d  %02d:%02d:%02d\n",
                timeinfo.tm_mday,     // tm_year is years since 1900
                timeinfo.tm_mon + 1,  // tm_mon is months since January (0-11)
                timeinfo.tm_year + 1900,
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);
}

void esp_ntp_stp() {
  sntp_set_time_sync_notification_cb(timeavailable);
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
sntp_setservername(0, const_cast<char*>(ntpServer1)); // Set primary NTP server
sntp_setservername(1, const_cast<char*>(ntpServer2)); // Set secondary NTP server
sntp_init();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  configTzTime(time_zone, ntpServer1, ntpServer2);
  while (!getLocalTime(&timeinfo)) {
    _PRINT_DEBUG_("❌   Failed to obtain time\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  got_time = true;

  _PRINT_DEBUG_("✅   NTP Time Ready\n");

  vTaskDelay(1000 / portTICK_PERIOD_MS);
  fetchData(timeinfo.tm_mday - 1, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, "Dusseldorf", "Germany");
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  checkPrayerTimes(timeinfo.tm_hour * 60 + timeinfo.tm_min);
}

// perform the actual update from a given stream
void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      _PRINT_DEBUG_("Written : %zu successfully\n", written);
    } else {
      _PRINT_DEBUG_("Written only : %zu/%zu. Retry?\n", written, updateSize);
    }
    if (Update.end()) {
      _PRINT_DEBUG_("OTA done!\n");
      if (Update.isFinished()) {
        _PRINT_DEBUG_("Update successfully completed. Rebooting.\n");
        SHOW_LED(0, 255, 0, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP.restart();
      } else {
        _PRINT_DEBUG_("Update not finished? Something went wrong!\n");
        SHOW_LED(255, 0, 0, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP.restart();
      }
    } else {
      _PRINT_DEBUG_("Error Occurred. Error #: %d\n", Update.getError());
    }
  } else {
    _PRINT_DEBUG_("Not enough space to begin OTA\n");
  }
}

void esp_sd_ota_stp() {
  File updateBin = SD_TYPE.open("/firmware.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      _PRINT_DEBUG_("Error, firmware.bin is not a file\n");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      _PRINT_DEBUG_("Try to start update\n");
      SHOW_LED(0, 0, 255, 0);
      performUpdate(updateBin, updateSize);
    } else {
      _PRINT_DEBUG_("Error, file is empty\n");
    }

    updateBin.close();
    // SD_TYPE.remove("/firmware.bin");
    SD_TYPE.rename("/firmware.bin", "/firmware_done.bin");

  } else {
    _PRINT_DEBUG_("Could not load firmware.bin from sd root\n");
  }
}

void esp_audio_stp() {
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(4);  // 0...21
  if (psramFound()) {
    audio.setBufsize(-1, 512 * 1024);  // Use 512 KB PSRAM if available
    _PRINT_DEBUG_("✅   Audio Buffer is set\n");
  }

  digitalWrite(I2S_SD, HIGH);
  audio.connecttoFS(SD_TYPE, tracks[5]);  // 5
  while (audio.isRunning()) {
    audio.loop();
    AudioVU();
  }
  _PRINT_DEBUG_("✅   Audio Ready\n");
}

void AudioVU() {
  if (audio.isRunning()) {
    uint16_t vum = audio.getVUlevel();
    uint8_t maxLevel = max((vum >> 8) & 0xFF, vum & 0xFF);
    int brightness = map(maxLevel, 0, 127, 0, 255);
    // int b = map(maxLevel, 0, 127, 255, 0);
    SHOW_LED(0, 0, 0, brightness);
  } else {
    SHOW_LED(0, 0, 0, 0);
  }
}

void audio_eof_mp3(const char *info) {  //end of file
  _PRINT_DEBUG_("🔊   End of Audio: %s\n", info);
}

void playAudio(const char *filename, int volume) {
  static const char *lastPlayedFile = nullptr;
  static bool isPlaying = false;

  if (!isPlaying || (lastPlayedFile && strcmp(lastPlayedFile, filename) != 0)) {
    isPlaying = true;
    lastPlayedFile = filename;
    audio.stopSong();
    audio.setVolume(volume);
    audio.connecttoFS(SD_TYPE, filename);
    _PRINT_DEBUG_("🔊 Playing: %s at volume %d\n", filename, volume);
  }
}

void playSequence(int vol, int repeatCount) {
  static int currentIndex = 0;
  static int remainingRepeats = 0;
  static bool playedKahf = false;
  static const char *repeatQueue[] = {
    tracks[7],  // Al-Fatiha
    tracks[8],  // Al-Ikhlas
    tracks[9],  // Al-Falaq
    tracks[10]  // An-Nas
  };
  static const char *kahfTrack = tracks[11];  // Al-Kahf (Only on Friday)

  int repeatQueueSize = sizeof(repeatQueue) / sizeof(repeatQueue[0]);

  if (!playSurahSequence) {
    currentIndex = 0;
    remainingRepeats = repeatCount;
    playedKahf = false;
  }

  if (!audio.isRunning() && playSurahSequence) {
    if (remainingRepeats > 0 && currentIndex < repeatQueueSize) {
      // Play repeated Surahs
      audio.setVolume(vol);
      audio.connecttoFS(SD_TYPE, repeatQueue[currentIndex]);

      const char *surahNames[] = {
        "Playing Surah Al-Fatiha",
        "Playing Surah Al-Ikhlas",
        "Playing Surah Al-Falaq",
        "Playing Surah An-Nas"
      };

      snprintf(PrayerData, sizeof(PrayerData), "%s", surahNames[currentIndex]);

      _PRINT_DEBUG_("🎶 Now Playing: %s\n", repeatQueue[currentIndex]);
      currentIndex++;
    } else if (remainingRepeats > 0) {
      // Reset index for another repeat cycle
      remainingRepeats--;
      currentIndex = 0;
    } else if (!playedKahf && strcmp(Weekday_Gregorian, "Friday") == 0) {
      // Play Al-Kahf once after all repetitions
      audio.setVolume(vol);
      audio.connecttoFS(SD_TYPE, kahfTrack);
      snprintf(PrayerData, sizeof(PrayerData), "It is Jumu'ah, playing Surah Al-Kahf");
      _PRINT_DEBUG_("🎶 Now Playing: %s\n", kahfTrack);
      playedKahf = true;
    } else {
      // End sequence
      playSurahSequence = false;
      PrayerData[0] = '\0';  // Clear PrayerData
    }
  }
}

void esp_neo_stp() {
  rgbwLED.begin();
  rgbwLED.clear();
}

void SHOW_LED(int R, int G, int B, int W) {
  static int lastR = -1, lastG = -1, lastB = -1, lastW = -1;  // Store last color values
  if (R != lastR || G != lastG || B != lastB || W != lastW) {
    rgbwLED.setPixelColor(0, rgbwLED.Color(R, G, B, W));
    rgbwLED.show();
    lastR = R;
    lastG = G;
    lastB = B;
    lastW = W;
  }
}

void esp_sd_stp() {
  //  SD_TYPE.setPins(MMC_CLK, MMC_CMD, MMC_D0, MMC_D1, MMC_D2, MMC_D3);
  while (!SD_TYPE.begin()) {
    _PRINT_DEBUG_("❌   SD Mount Failed\n");
    SHOW_LED(255, 0, 0, 0);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    SHOW_LED(0, 0, 0, 0);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }

  uint8_t cardType = SD_TYPE.cardType();
  if (cardType == CARD_NONE) {
    _PRINT_DEBUG_("❌   No SD card attached\n");
    SHOW_LED(255, 0, 0, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    SHOW_LED(0, 0, 0, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return;
  }

  _PRINT_DEBUG_("✅   SD Card Type: ");
  if (cardType == CARD_MMC) {
    _PRINT_DEBUG_("MMC\n");
  } else if (cardType == CARD_SD) {
    _PRINT_DEBUG_("SDSC\n");
  } else if (cardType == CARD_SDHC) {
    _PRINT_DEBUG_("SDHC\n");
  } else {
    _PRINT_DEBUG_("UNKNOWN\n");
  }

  _PRINT_DEBUG_("⚠️   SD Card Size : %s\n", formatBytes(SD_TYPE.cardSize()));
  _PRINT_DEBUG_("⚠️   SD Used space: %s\n", formatBytes(SD_TYPE.usedBytes()));
  listDir("/", 0, true);
}
