// Audio
// Booster_audio_SPIFFS (1.8MB OTA/0.5MB)


//# 1835008 B App , 393216 B Spiffs  (1.835008 , 393.216)
//# Name,   Type, SubType, Offset,  Size, Flags
//# nvs,      data, nvs,     0x9000,  0x5000,
//# otadata,  data, ota,     0xE000,  0x2000,
//# app0,     app,  ota_0,   0x10000, 0x1C0000,
//# app1,     app,  ota_1,   0x1D0000,0x1C0000,
//# spiffs,   data, spiffs,  0x390000,0x60000,
//# coredump, data, coredump,0x3F0000,0x10000,
//
//# 1769472 B App , 524288 B Spiffs  (1.769472 , 524.288)
//# Name,   Type, SubType, Offset,  Size, Flags
//nvs,      data, nvs,     0x9000,  0x5000,
//otadata,  data, ota,     0xe000,  0x2000,
//app0,     app,  ota_0,   0x10000, 0x1B0000,
//app1,     app,  ota_1,   0x1C0000,0x1B0000,
//spiffs,   data, spiffs,  0x370000,0x80000,
//coredump, data, coredump,0x3F0000,0x10000,


//esp32.menu.PartitionScheme.Booster=Booster_Audio (1.7MB OTA/0.5MB)
//esp32.menu.PartitionScheme.Booster.build.partitions=booster_audio_spiffs
//esp32.menu.PartitionScheme.Booster.upload.maximum_size=1769472


#include <WiFiManager.h>
#include <ArduinoOTA.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerialLite.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm> // For std::sort
#include "time.h"
#include "esp_sntp.h"

//I2S MAX98357
#include "Audio.h"
#include "SPIFFS.h"

//#include <OneButton.h>
//#include <Wire.h>
//#include <Adafruit_AHTX0.h> // SDA 4  SCL 5
//#include <SFE_BMP180.h>


#define INDOOR_PIN       18
#define OUTDOOR_PIN      19

#define I2S_LRC       25
#define I2S_BCLK      26
#define I2S_DOUT      27
#define I2S_SD        33

const char * Name = "Prayer_Hub";
const char * Pass = "12345678";


const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

const char* time_zone = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
struct tm timeinfo;

unsigned long Print_LastMillis = 0, FetchData_LastMillis = 0;
int Minute = 0, Hour = 0, Day = 0, Month = 0, Year = 0;
String City = "Dusseldorf", Country = "Germany";

String Time_Fajr, Time_Dhuhr, Time_Asr, Time_Maghrib, Time_Isha;
String Date_hijri, Month_hijri;
bool prayerTimesFetched = false;
int currentTrack = -1;
bool trackChanged = false;
bool isPlaying = false; // Flag to indicate if a track is currently playing

char tempBuffer[256];

int AudioVol = 10;


const char* tracks[] = {
  "",
  "1-Fajr.mp3",
  "2-Dhuhr.mp3",
  "3-Asr.mp3",
  "4-Maghrib.mp3",
  "5-Isha.mp3",
  "6-Azan-N.mp3",
  "7-Azan.mp3"
};


WiFiManager wifiManager; // global wm instance
Audio audio;
AsyncWebServer server(80);

//#define _PRINT_DEBUG_(msg) Serial.print(msg)
//#define _PRINT_DEBUG_(msg) WebSerial.print(msg)
#define _PRINT_DEBUG_(msg) { Serial.print(msg); WebSerial.print(msg); }


void audio_eof_mp3(const char *info) {
  snprintf(tempBuffer, sizeof(tempBuffer), "\nEnd of mp3: %s\n", info);
  _PRINT_DEBUG_(tempBuffer);
  isPlaying = false; // Reset playback status when a track finishes
}

void selectTrack(int trackIndex, bool forcePlay = false) {
  if ((currentTrack != trackIndex || forcePlay) && !isPlaying) {
    currentTrack = trackIndex;
    trackChanged = true;
    isPlaying = true; // Mark as playing
    snprintf(tempBuffer, sizeof(tempBuffer), "Playing: %d\n", currentTrack);
    _PRINT_DEBUG_(tempBuffer);
    if (forcePlay) {
      audio.stopSong();
      isPlaying = false; // Reset isPlaying since stopSong will halt playback
      _PRINT_DEBUG_("Replay\n");
    }
  }
}


void Audio_Task(void *pvParameters ) {
  (void) pvParameters;
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, LOW);

  if (!SPIFFS.begin()) { //!SPIFFS.begin(true)
    _PRINT_DEBUG_("SPIFFS Mount Failed\n");
    return;
  }
  listDir(SPIFFS, "/", 0);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(AudioVol); // 0...21
  vTaskDelay(1);

  digitalWrite(I2S_SD, HIGH);
  audio.connecttoFS(SPIFFS, tracks[6]);
  audio.loop();
  Serial.print("Audio Ready\n");
  vTaskDelay(1);

  for (;;) {
    if (!audio.isRunning() && isPlaying) {
      isPlaying = false; // Correctly reflect that audio has stopped
    }

    if (trackChanged && !isPlaying) {
      audio.connecttoFS(SPIFFS, tracks[currentTrack]);
      trackChanged = false;
      isPlaying = true; // Ensure isPlaying is true when starting a new track
      snprintf(tempBuffer, sizeof(tempBuffer), "%s  - initialized", tracks[currentTrack]);
      _PRINT_DEBUG_(tempBuffer);
    }

    digitalWrite(I2S_SD, audio.isRunning() ? HIGH : LOW);
    audio.loop();

    vTaskDelay(5);
  }
}


// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t)
{
  _PRINT_DEBUG_("Got time adjustment from NTP!\n");
  checkPrayerTimes();

  snprintf(tempBuffer, sizeof(tempBuffer), "%02d-%02d-%04d %02d:%02d:%02d\n",
           timeinfo.tm_mday, // tm_year is years since 1900
           timeinfo.tm_mon + 1,     // tm_mon is months since January (0-11)
           timeinfo.tm_year + 1900,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  _PRINT_DEBUG_(tempBuffer);
}

void checkPrayerTimes() {
  // Assume getLocalTime(&timeinfo) has been called and timeinfo is updated.
  while (!getLocalTime(&timeinfo)) {
    _PRINT_DEBUG_("Failed to obtain time\n");
    vTaskDelay(100);
  }

  // Accessing day, month, and year from timeinfo and adjusting month and year to familiar format
  Day = timeinfo.tm_mday - 1; // Day of the month (1-31)
  Month = timeinfo.tm_mon + 1; // Months since January (0-11), so add 1 for familiar month (1-12)
  Year = timeinfo.tm_year + 1900; // Years since 1900


  if (!prayerTimesFetched) {
    //    _PRINT_DEBUG_("Prayer times not fetched yet.\n");
    return; // Exit if prayer times haven't been fetched
  }

  // Current time in minutes since midnight
  int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  // Function to convert prayer time string to minutes since midnight
  auto timeStringToMinutes = [](const String & timeStr) -> int {
    int hour = timeStr.substring(0, timeStr.indexOf(':')).toInt();
    int minute = timeStr.substring(timeStr.indexOf(':') + 1).toInt();
    return hour * 60 + minute;
  };

  // Define a structure to hold prayer time information
  struct PrayerTime {
    String name;
    int minutesSinceMidnight;
  };

  // Initialize an array of prayer times
  std::vector<PrayerTime> prayerTimes = {
    {"Fajr", timeStringToMinutes(Time_Fajr)},
    {"Dhuhr", timeStringToMinutes(Time_Dhuhr)},
    {"Asr", timeStringToMinutes(Time_Asr)},
    {"Maghrib", timeStringToMinutes(Time_Maghrib)},
    {"Isha", timeStringToMinutes(Time_Isha)}
  };

  // Sort prayer times to ensure they are in chronological order
  std::sort(prayerTimes.begin(), prayerTimes.end(), [](const PrayerTime & a, const PrayerTime & b) {
    return a.minutesSinceMidnight < b.minutesSinceMidnight;
  });


  // Check for current and next prayer times
  for (size_t i = 0; i < prayerTimes.size(); i++) {
    if (nowMinutes == prayerTimes[i].minutesSinceMidnight) {
      snprintf(tempBuffer, sizeof(tempBuffer), "It's time for %s\n", prayerTimes[i].name.c_str());
      _PRINT_DEBUG_(tempBuffer);
      selectTrack(7, false);
    } else if (nowMinutes == prayerTimes[i].minutesSinceMidnight - 1) {
      snprintf(tempBuffer, sizeof(tempBuffer), "1 minutes until %s\n", prayerTimes[i].name.c_str());
      _PRINT_DEBUG_(tempBuffer);
      selectTrack(i + 1, false);
    }

    // Determine and notify the next prayer time
    if (nowMinutes < prayerTimes[i].minutesSinceMidnight) {
      // This is the next prayer time
      int minutesUntilNextPrayer = prayerTimes[i].minutesSinceMidnight - nowMinutes;
      snprintf(tempBuffer, sizeof(tempBuffer), "Next prayer is  %s in %d minutes.\n", prayerTimes[i].name.c_str(), minutesUntilNextPrayer);
      _PRINT_DEBUG_(tempBuffer);
      break; // Exit the loop once the next prayer time is found
    }
  }

  snprintf(tempBuffer, sizeof(tempBuffer), "%02d-%02d-%04d %02d:%02d:%02d - ",
           timeinfo.tm_mday, // tm_year is years since 1900
           timeinfo.tm_mon + 1,     // tm_mon is months since January (0-11)
           timeinfo.tm_year + 1900,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  _PRINT_DEBUG_(tempBuffer);

  snprintf(tempBuffer, sizeof(tempBuffer), "%s - %s - F:%s D:%s A:%s M:%s I:%s\n\n",
           Date_hijri.c_str(), Month_hijri.c_str(), Time_Fajr.c_str(),
           Time_Dhuhr.c_str(), Time_Asr.c_str(), Time_Maghrib.c_str(), Time_Isha.c_str());
  _PRINT_DEBUG_(tempBuffer);
}


String getTimeFromString(String timeStr) {
  return timeStr.substring(0, timeStr.indexOf(' ')); // Remove "(CET)" or similar
}


void Timer_Task(void *pvParameters ) {
  (void) pvParameters;
  sntp_set_time_sync_notification_cb( timeavailable );
  sntp_servermode_dhcp(1);    // (optional)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  configTzTime(time_zone, ntpServer1, ntpServer2);
  Serial.print("Time Ready\n");
  vTaskDelay(1);
  for (;;) {
    checkPrayerTimes();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}


void recvMsg(uint8_t *data, size_t len) {
  Serial.print("Received Data...\n");
  String d = "";
  for (int i = 0; i < len; i++) {
    d += char(data[i]);
  }
  Serial.print(d + "\n");
}

void configModeCallback (WiFiManager *myWiFiManager) {
  snprintf(tempBuffer, sizeof(tempBuffer), "Entered config mode\nIP: %s\nSSID: %s\n",
           WiFi.softAPIP().toString().c_str(),
           myWiFiManager->getConfigPortalSSID().c_str());
  _PRINT_DEBUG_(tempBuffer);
}

void esp_wifi_stp() {
  WiFi.mode(WIFI_STA);
  wifiManager.setClass("invert");
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setAPClientCheck(true); // avoid timeout if client connected to softap
  wifiManager.setScanDispPerc(true);       // show RSSI as percentage not graph icons
  wifiManager.setConnectRetries(3);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setHostname(Name);

  if (!wifiManager.autoConnect(Name, Pass)) {
    Serial.print("failed to connect and hit timeout\n");
    for (int i = 0; i <= 9; i++) {
      //      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      vTaskDelay(500);
    }
    //    digitalWrite(LED_BUILTIN, 1);
    ESP.restart();
  }

  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  /* Attach Message Callback */
  WebSerial.onMessage(recvMsg);
  server.begin();
  Serial.print("WebSerial Ready\n");
}


void esp_ota_stp() {
  ArduinoOTA.setHostname(Name);
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    snprintf(tempBuffer, sizeof(tempBuffer), "Progress: %u%%\n", (progress / (total / 100)));
    _PRINT_DEBUG_(tempBuffer);
    //    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  });
  ArduinoOTA.begin();
  Serial.print("OTA Ready\n");
}

void setup() {
  Serial.begin(115200);
  esp_wifi_stp();
  esp_ota_stp();
  xTaskCreatePinnedToCore(Audio_Task, "Audio_Task", 12 * 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(Timer_Task, "Timer_Task", 8 * 1024, NULL, 1, NULL, 1);
  vTaskDelay(15000);
  xTaskCreatePinnedToCore(GetData_Task, "GetData_Task", 16 * 1024, NULL, 2, NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
}

void GetData_Task(void *pvParameters ) {
  (void) pvParameters;
  int lastAttemptHour = -1;
  Serial.print("Getting Data Ready\n");
  vTaskDelay(1);

  for (;;) {
    int currentHour = timeinfo.tm_hour;

    //    if (Serial.available()) {
    //      String hourInput = Serial.readStringUntil('\n'); // Read the input until newline
    //      currentHour = hourInput.toInt(); // Convert input to integer
    //      _PRINT_DEBUG_("Debugging hour set to: ");
    //      _PRINT_DEBUG_(currentHour);
    //      _PRINT_DEBUG_("\n");
    //    }

    if (!prayerTimesFetched) {
      fetchData();
    }
    if ((currentHour % 6 == 0) && (lastAttemptHour != currentHour)) {
      fetchData();
      lastAttemptHour = currentHour;
      snprintf(tempBuffer, sizeof(tempBuffer), "[HTTP] Fetching Data [%d]\n", currentHour);
      _PRINT_DEBUG_(tempBuffer);
    }
    vTaskDelay(60000 / portTICK_PERIOD_MS);
  }
}

void fetchData() {
  if (WiFi.status() == WL_CONNECTED) {
    const int maxRetries = 8; // Maximum number of retries
    int retryCount = 0; // Current retry attempt
    bool requestSuccessful = false; // Flag to indicate if the request was successful

    if (!requestSuccessful && retryCount < maxRetries) {
      HTTPClient http;

      //"http://api.aladhan.com/v1/calendarByCity/2024/2?city=Dusseldorf&country=Germany&method=3"
      String url = "http://api.aladhan.com/v1/calendarByCity/";
      url += Year;
      url += "/";
      url += Month;
      url += "?city=" + City + "&country=" + Country + "&method=3";

      snprintf(tempBuffer, sizeof(tempBuffer), "[HTTP] fetchData begin...\n%s\n", url.c_str());
      _PRINT_DEBUG_(tempBuffer);

      if (http.begin(url)) {  // API Request
        int httpCode = http.GET();
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          snprintf(tempBuffer, sizeof(tempBuffer), "[HTTP] fetchData GET... code: %d\n", httpCode);
          _PRINT_DEBUG_(tempBuffer);
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            auto payload = http.getString();

            JsonDocument doc; // Adjust size as needed for your JSON data
            JsonDocument filter; // Adjust size as needed for filtering
            filter["data"][0]["timings"]["Fajr"] = true;
            filter["data"][0]["timings"]["Dhuhr"] = true;
            filter["data"][0]["timings"]["Asr"] = true;
            filter["data"][0]["timings"]["Maghrib"] = true;
            filter["data"][0]["timings"]["Isha"] = true;
            filter["data"][0]["date"]["hijri"]["date"] = true;
            filter["data"][0]["date"]["hijri"]["month"]["en"] = true;

            DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
            if (!error) {
              //              serializeJsonPretty(doc, Serial); Serial.println("-------------------------");

              JsonObject Data = doc["data"][Day];

              Time_Fajr = getTimeFromString(Data["timings"]["Fajr"].as<String>());
              Time_Dhuhr = getTimeFromString(Data["timings"]["Dhuhr"].as<String>());
              Time_Asr = getTimeFromString(Data["timings"]["Asr"].as<String>());
              Time_Maghrib = getTimeFromString(Data["timings"]["Maghrib"].as<String>());
              Time_Isha = getTimeFromString(Data["timings"]["Isha"].as<String>());
              //              Time_Maghrib = "17:25";
              //              Time_Isha = "17:30";

              Date_hijri = Data["date"]["hijri"]["date"].as<String>();
              Month_hijri = Data["date"]["hijri"]["month"]["en"].as<String>();

              //              serializeJsonPretty(Data, Serial); Serial.println("-------------------------");

              requestSuccessful = true; // Mark the request as successful
              prayerTimesFetched = true;
            } else {
              snprintf(tempBuffer, sizeof(tempBuffer), "deserializeJson() failed: %s\n", error.c_str());
              _PRINT_DEBUG_(tempBuffer);
              prayerTimesFetched = false;
            }
          }
        } else {
          snprintf(tempBuffer, sizeof(tempBuffer), "[HTTP] fetchData GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          _PRINT_DEBUG_(tempBuffer);
          prayerTimesFetched = false;
        }

        http.end();
      } else {
        _PRINT_DEBUG_("[HTTP} Unable to connect to fetchData\n");
        prayerTimesFetched = false;
      }

      if (!requestSuccessful) {
        retryCount++;
        _PRINT_DEBUG_("Retrying...\n");
        vTaskDelay(5000);
      }
    }
    if (!requestSuccessful) {
      _PRINT_DEBUG_("Failed to fetch data after retries.\n");
      prayerTimesFetched = false;
    }
  }
}


void listDir(fs::FS & fs, const char * dirname, uint8_t levels) {
  snprintf(tempBuffer, sizeof(tempBuffer), "\n\nListing directory: %s\r\n", dirname);
  _PRINT_DEBUG_(tempBuffer);

  File root = fs.open(dirname);
  if (!root) {
    _PRINT_DEBUG_("- failed to open directory\n");
    return;
  }
  if (!root.isDirectory()) {
    _PRINT_DEBUG_(" - not a directory\n");
    return;
  }
  size_t TotalSize = 0;
  int FilesCount = 0;
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      snprintf(tempBuffer, sizeof(tempBuffer), "  DIR : %s\n", file.name());
      _PRINT_DEBUG_(tempBuffer);
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      snprintf(tempBuffer, sizeof(tempBuffer), "SIZE: %s\t\tFILE: %s\n", formatBytes(file.size()).c_str(), file.name());
      _PRINT_DEBUG_(tempBuffer);
      TotalSize += file.size();
      FilesCount += 1;
    }
    file = root.openNextFile();
  }
  snprintf(tempBuffer, sizeof(tempBuffer), "\nTOTAL SIZE: %s\t\tFILES Count: %d\n\n", formatBytes(TotalSize).c_str(), FilesCount);
  _PRINT_DEBUG_(tempBuffer);
}

//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}
