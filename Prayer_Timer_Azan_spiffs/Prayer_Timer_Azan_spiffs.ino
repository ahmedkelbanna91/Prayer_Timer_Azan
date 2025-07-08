#include <vector>
#include <algorithm>  // For std::sort
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include "esp_sntp.h"
#include "Audio.h"  //I2S MAX98357
#include <Adafruit_NeoPixel.h>


#define SD_CARD

// =================================================
#ifdef SD_CARD
// No FS 4MB (2MB APP x2)
#define Folder "/HD/"
#include "SPI.h"
#include "SD.h"
#include "FS.h"

#define SD_CS 17
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

#include <NetworkClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

static bool hasSD = false;
WebServer server(80);
File uploadFile;

#else
#define Folder "/SPIFFS/"
#include "SPIFFS.h"
// Audio
// audio_SPIFFS (1.8MB OTA/0.5MB)

//# 1769472 B App , 524288 B Spiffs  (1.769472 , 524.288)
//# Name,   Type, SubType, Offset,  Size, Flags
//nvs,      data, nvs,     0x9000,  0x5000,
//otadata,  data, ota,     0xe000,  0x2000,
//app0,     app,  ota_0,   0x10000, 0x1B0000,
//app1,     app,  ota_1,   0x1C0000,0x1B0000,
//spiffs,   data, spiffs,  0x370000,0x80000,
//coredump, data, coredump,0x3F0000,0x10000,


// esp32.menu.PartitionScheme.Audio=Audio (1.8MB OTA/0.5MB)
// esp32.menu.PartitionScheme.Audio.build.partitions=audio_spiffs
// esp32.menu.PartitionScheme.Audio.upload.maximum_size=1769472
#endif

#define LED_PIN 16  // Digital pin connected to the SK6812 data line
#define I2S_LRC 25
#define I2S_BCLK 26
#define I2S_DOUT 27
#define I2S_SD 33

const char *Name = "Azan_Hub";
const char *Pass = "12345678";


const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

const char *time_zone = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
struct tm timeinfo;

unsigned long Print_LastMillis = 0, FetchData_LastMillis = 0;
int Minute = 0, Hour = 0, Day = 0, Month = 0, Year = 0;
String City = "Dusseldorf", Country = "Germany";

String Time_Fajr, Time_Dhuhr, Time_Asr, Time_Maghrib, Time_Isha;
String Date_hijri, Month_hijri;
bool prayerTimesFetched = false;
int currentTrack = -1;
bool trackChanged = false;
bool isPlaying = false;  // Flag to indicate if a track is currently playing
bool OTA = false;

const char *tracks[] = { "",
                         "Fajr.mp3", "Dhuhr.mp3", "Asr.mp3",
                         "Maghrib.mp3", "Isha.mp3", "Azan-N.mp3", "Azan.mp3" };


WiFiManager wifiManager;  // global wm instance
Audio audio;
Adafruit_NeoPixel rgbwLED(1, LED_PIN, NEO_GRBW + NEO_KHZ800);


#define _PRINT_DEBUG_(...) debugPrint(__VA_ARGS__)

void debugPrint(const char *format, ...) {
  va_list args;
  va_start(args, format);

  char buffer[1024];  // Buffer to store formatted string
  vsnprintf(buffer, sizeof(buffer), format, args);

  Serial.print(buffer);
  va_end(args);
}


// void audio_info(const char *info) {
//   Serial.print("info        ");
//   Serial.println(info);
// }
// void audio_id3data(const char *info) {  //id3 metadata
//   _PRINT_DEBUG_("Start of mp3:     %s\n", info);
// }
void audio_eof_mp3(const char *info) {
  _PRINT_DEBUG_("End of mp3 :     %s\n", info);

  isPlaying = false;  // Reset playback status when a track finishes
}

void selectTrack(int trackIndex, bool forcePlay, int Volume) {
  audio.setVolume(Volume);
  if ((currentTrack != trackIndex || forcePlay) && !isPlaying) {
    currentTrack = trackIndex;
    trackChanged = true;
    isPlaying = true;  // Mark as playing
    _PRINT_DEBUG_("Playing: %d\n", currentTrack);

    if (forcePlay) {
      audio.stopSong();
      isPlaying = false;  // Reset isPlaying since stopSong will halt playback
      _PRINT_DEBUG_("Replay\n");
    }
  }
}


void Audio_Task(void *pvParameters) {
  (void)pvParameters;
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, LOW);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(4);  // 0...21

  //   vTaskDelay(5000 / portTICK_PERIOD_MS);
  //   digitalWrite(I2S_SD, HIGH);
  // #ifdef SD_CARD
  //   audio.connecttoFS(SD, (String(Folder) + tracks[6]).c_str());  // 6
  // #else
  //   audio.connecttoFS(SPIFFS, (String(Folder) + tracks[6]).c_str());  // 6
  // #endif

  _PRINT_DEBUG_("\nAudio Ready\n\n");
  vTaskDelay(1 / portTICK_PERIOD_MS);

  for (;;) {
    if (!OTA) {
      if (!audio.isRunning() && isPlaying) {
        isPlaying = false;  // Correctly reflect that audio has stopped
      }

      if (trackChanged && !isPlaying) {
#ifdef SD_CARD
        audio.connecttoFS(SD, (String(Folder) + tracks[currentTrack]).c_str());
#else
        audio.connecttoFS(SPIFFS, (String(Folder) + tracks[currentTrack]).c_str());
#endif
        trackChanged = false;
        isPlaying = true;  // Ensure isPlaying is true when starting a new track
        _PRINT_DEBUG_("%s  - initialized", tracks[currentTrack]);
      }

      if (Serial.available() && !isPlaying) {
        int trackNumber = Serial.parseInt();  // Read integer from Serial input

        if (trackNumber >= 1 && trackNumber < sizeof(tracks) / sizeof(tracks[0])) {
          currentTrack = trackNumber;
          _PRINT_DEBUG_("Track changed to: %d - %s\n", currentTrack, tracks[currentTrack]);
          audio.connecttoFS(SD, (String(Folder) + tracks[currentTrack]).c_str());
        }
      }

      digitalWrite(I2S_SD, audio.isRunning() ? HIGH : LOW);
      audio.loop();
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t) {
  _PRINT_DEBUG_("Got time adjustment from NTP!\n");
  _PRINT_DEBUG_("%02d.%02d.%04d  %02d:%02d:%02d\n",
                timeinfo.tm_mday,     // tm_year is years since 1900
                timeinfo.tm_mon + 1,  // tm_mon is months since January (0-11)
                timeinfo.tm_year + 1900,
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);
}

void checkPrayerTimes() {
  // Assume getLocalTime(&timeinfo) has been called and timeinfo is updated.
  while (!getLocalTime(&timeinfo)) {
    _PRINT_DEBUG_("Failed to obtain time\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  // Accessing day, month, and year from timeinfo and adjusting month and year to familiar format
  Day = timeinfo.tm_mday - 1;      // Day of the month (1-31)
  Month = timeinfo.tm_mon + 1;     // Months since January (0-11), so add 1 for familiar month (1-12)
  Year = timeinfo.tm_year + 1900;  // Years since 1900


  if (!prayerTimesFetched) {
    _PRINT_DEBUG_("Prayer times not fetched yet.\n");
    return;  // Exit if prayer times haven't been fetched
  }

  // Current time in minutes since midnight
  int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  // Function to convert prayer time string to minutes since midnight
  auto timeStringToMinutes = [](const String &timeStr) -> int {
    int hour = timeStr.substring(0, timeStr.indexOf(':')).toInt();
    int minute = timeStr.substring(timeStr.indexOf(':') + 1).toInt();
    return hour * 60 + minute;
  };

  // Define a structure to hold prayer time information
  struct PrayerTime {
    String name;
    int vol;
    int minutesSinceMidnight;
  };

  // Initialize an array of prayer times
  std::vector<PrayerTime> prayerTimes = {
    { "Fajr", 2, timeStringToMinutes(Time_Fajr) },
    { "Dhuhr", 8, timeStringToMinutes(Time_Dhuhr) },
    { "Asr", 8, timeStringToMinutes(Time_Asr) },
    { "Maghrib", 8, timeStringToMinutes(Time_Maghrib) },
    { "Isha", 4, timeStringToMinutes(Time_Isha) }
  };

  // Sort prayer times to ensure they are in chronological order
  std::sort(prayerTimes.begin(), prayerTimes.end(), [](const PrayerTime &a, const PrayerTime &b) {
    return a.minutesSinceMidnight < b.minutesSinceMidnight;
  });

  // Check for current and next prayer times
  for (size_t i = 0; i < prayerTimes.size(); i++) {
    if (nowMinutes == prayerTimes[i].minutesSinceMidnight) {
      _PRINT_DEBUG_("It's time for %s\n", prayerTimes[i].name.c_str());

      selectTrack(7, false, prayerTimes[i].vol);
    } else if (nowMinutes == prayerTimes[i].minutesSinceMidnight - 1) {
      _PRINT_DEBUG_("1 minutes until %s\n", prayerTimes[i].name.c_str());

      selectTrack(i + 1, false, prayerTimes[i].vol);
    }

    // Determine and notify the next prayer time
    if (nowMinutes < prayerTimes[i].minutesSinceMidnight) {
      // This is the next prayer time
      int minutesUntilNextPrayer = prayerTimes[i].minutesSinceMidnight - nowMinutes;
      _PRINT_DEBUG_("Next prayer is  %s in %d minutes.\n", prayerTimes[i].name.c_str(), minutesUntilNextPrayer);

      break;  // Exit the loop once the next prayer time is found
    }
  }

  _PRINT_DEBUG_("%02d:%02d:%02d  %02d.%02d.%04d  %s-%s  F%s D%s A%s M%s I%s\n\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                Date_hijri.c_str(), Month_hijri.c_str(), Time_Fajr.c_str(), Time_Dhuhr.c_str(),
                Time_Asr.c_str(), Time_Maghrib.c_str(), Time_Isha.c_str());
}


String getTimeFromString(String timeStr) {
  return timeStr.substring(0, timeStr.indexOf(' '));  // Remove "(CET)" or similar
}

void fetchData() {
  if (WiFi.status() != WL_CONNECTED) {
    _PRINT_DEBUG_("[WiFi] Not connected, skipping fetchData\n");
    return;
  }

  //"http://api.aladhan.com/v1/calendarByCity/2025/1?city=Dusseldorf&country=Germany&method=3"
  String url = "http://api.aladhan.com/v1/calendarByCity/";
  url += String(Year);
  url += "/";
  url += String(Month);
  url += "?city=" + City + "&country=" + Country + "&method=3";

  _PRINT_DEBUG_("[HTTP] fetchData begin...\n%s\n", url.c_str());

  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);  // 10 seconds timeout

  if (!http.begin(url)) {
    _PRINT_DEBUG_("[HTTP] Failed to initialize HTTP request\n");
    prayerTimesFetched = false;
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    return;
  }

  // _PRINT_DEBUG_("[MEMORY] Free heap before request: %d\n", ESP.getFreeHeap());

  int httpCode = http.GET();
  _PRINT_DEBUG_("[HTTP] fetchData GET... code: %d\n", httpCode);
  if (httpCode == 429) {  // Too Many Requests
    _PRINT_DEBUG_("[HTTP] API rate limit reached. Waiting before retrying...\n");
    vTaskDelay(60000 / portTICK_PERIOD_MS);  // Wait 1 minute
  }

  if (httpCode != 200) {
    prayerTimesFetched = false;
    _PRINT_DEBUG_("[HTTP] Request failed: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    return;
  }

  JsonDocument filter;
  filter["data"][0]["timings"]["Fajr"] = true;
  filter["data"][0]["timings"]["Dhuhr"] = true;
  filter["data"][0]["timings"]["Asr"] = true;
  filter["data"][0]["timings"]["Maghrib"] = true;
  filter["data"][0]["timings"]["Isha"] = true;
  filter["data"][0]["date"]["hijri"]["date"] = true;
  filter["data"][0]["date"]["hijri"]["month"]["en"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

  http.end();

  // _PRINT_DEBUG_("[MEMORY] Free heap after http end: %d\n", ESP.getFreeHeap());

  if (error) {
    prayerTimesFetched = false;
    _PRINT_DEBUG_("[JSON] Parsing failed: %s\n", error.c_str());
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    return;
  }

  JsonObject dataObject = doc["data"][Day];
  if (dataObject.isNull()) {
    _PRINT_DEBUG_("[JSON] Data object is empty\n");
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    return;
  }

  Time_Fajr = getTimeFromString(dataObject["timings"]["Fajr"].as<String>());
  Time_Dhuhr = getTimeFromString(dataObject["timings"]["Dhuhr"].as<String>());
  Time_Asr = getTimeFromString(dataObject["timings"]["Asr"].as<String>());
  Time_Maghrib = getTimeFromString(dataObject["timings"]["Maghrib"].as<String>());
  Time_Isha = getTimeFromString(dataObject["timings"]["Isha"].as<String>());

  Date_hijri = dataObject["date"]["hijri"]["date"].as<String>();
  Month_hijri = dataObject["date"]["hijri"]["month"]["en"].as<String>();

  prayerTimesFetched = true;

  rgbwLED.setPixelColor(0, rgbwLED.Color(0, 255, 0, 0));
  rgbwLED.show();
  _PRINT_DEBUG_("Prayer times fetched successfully\n");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  _PRINT_DEBUG_("Entered config mode\nIP: %s\nSSID: %s\n",
                WiFi.softAPIP().toString().c_str(),
                myWiFiManager->getConfigPortalSSID().c_str());
}

void esp_wifi_stp() {
  WiFi.mode(WIFI_STA);
  wifiManager.setClass("invert");
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setAPClientCheck(true);  // avoid timeout if client connected to softap
  wifiManager.setScanDispPerc(true);   // show RSSI as percentage not graph icons
  wifiManager.setConnectRetries(3);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setHostname(Name);

  if (!wifiManager.autoConnect(Name, Pass)) {
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    _PRINT_DEBUG_("failed to connect and hit timeout\n");
    for (int i = 0; i <= 9; i++) {
      //      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    //    digitalWrite(LED_BUILTIN, 1);
    ESP.restart();
  }
  rgbwLED.setPixelColor(0, rgbwLED.Color(0, 255, 0, 0));
  rgbwLED.show();

  vTaskDelay(2000 / portTICK_PERIOD_MS);
  rgbwLED.setPixelColor(0, rgbwLED.Color(0, 0, 0, 0));
  rgbwLED.show();
}


void esp_ota_stp() {
  ArduinoOTA.setHostname(Name);
  ArduinoOTA.onStart([]() {
    OTA = true;
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
    else type = "filesystem";

    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    _PRINT_DEBUG_("Start OTA updating %s", type);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    OTA = true;
    uint8_t percentage = (progress * 100) / total;

    uint8_t red = 255 - ((percentage * 255) / 100);
    uint8_t green = (percentage * 255) / 100;

    _PRINT_DEBUG_("OTA Progress: %u%%\n", percentage);
    rgbwLED.setPixelColor(0, rgbwLED.Color(red, green, 0, 0));
    rgbwLED.show();
  });
  ArduinoOTA.onEnd([]() {
    OTA = false;
    _PRINT_DEBUG_("\nOTA End");
    rgbwLED.setPixelColor(0, rgbwLED.Color(0, 255, 0, 0));
    rgbwLED.show();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  });
  ArduinoOTA.begin();
  _PRINT_DEBUG_("OTA Ready\n");
}

void esp_webserver_stp() {
  if (MDNS.begin(Name)) {
    MDNS.addService("http", "tcp", 80);
    _PRINT_DEBUG_("MDNS responder started\n");
    _PRINT_DEBUG_("You can now connect to http://%s.local\n", Name);
  }

  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on(
    "/edit", HTTP_POST, []() {
      returnOK();
    },
    handleFileUpload);
  server.onNotFound(handleNotFound);

  server.begin();
  _PRINT_DEBUG_("HTTP server started");
}

void rgbwLED_setup() {
  rgbwLED.begin();
  rgbwLED.setBrightness(255);
  rgbwLED.setPixelColor(0, rgbwLED.Color(0, 0, 0, 0));
  rgbwLED.show();
}

void setup() {
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  Serial.begin(115200);
  rgbwLED_setup();
  esp_wifi_stp();
  esp_ota_stp();

#ifdef SD_CARD
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  SPI.setFrequency(1000000);
  if (!SD.begin(SD_CS)) {
    _PRINT_DEBUG_("SD Mount Failed\n");
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    rgbwLED.setPixelColor(0, rgbwLED.Color(0, 0, 0, 0));
    rgbwLED.show();
    hasSD = false;
    return;
  } else {
    hasSD = true;
  }
  listDir(SD, "/", 0);

  esp_webserver_stp();
#else
  if (!SPIFFS.begin()) {
    _PRINT_DEBUG_("SPIFFS Mount Failed\n");
    rgbwLED.setPixelColor(0, rgbwLED.Color(255, 0, 0, 0));
    rgbwLED.show();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    rgbwLED.setPixelColor(0, rgbwLED.Color(0, 0, 0, 0));
    rgbwLED.show();
    return;
  }
  listDir(SPIFFS, "/", 0);
#endif


  sntp_set_time_sync_notification_cb(timeavailable);
  // sntp_servermode_dhcp(1);    // (optional)
  esp_sntp_servermode_dhcp(1);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  configTzTime(time_zone, ntpServer1, ntpServer2);
  _PRINT_DEBUG_("Time Ready\n");

  vTaskDelay(1000 / portTICK_PERIOD_MS);

  xTaskCreatePinnedToCore(Audio_Task, "Audio_Task", 12 * 1024, NULL, 1, NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  vTaskDelay(1 / portTICK_PERIOD_MS);

  if (!OTA) {
    static int lastAttemptHour = -1;
    static int lastMin = -1;

    if (timeinfo.tm_min != lastMin) {
      lastMin = timeinfo.tm_min;
      checkPrayerTimes();
    }

    if (!prayerTimesFetched) {
      fetchData();
    } else {
      rgbwLED.setPixelColor(0, rgbwLED.Color(0, 0, 0, 0));
      rgbwLED.show();
      if ((timeinfo.tm_hour % 12 == 0) && (timeinfo.tm_hour != lastAttemptHour)) {
        fetchData();
        lastAttemptHour = timeinfo.tm_hour;
        _PRINT_DEBUG_("[HTTP] Fetching Data for checkup [%d]\n", timeinfo.tm_hour);
      }
    }
  }
}



size_t getDirSize(fs::FS &fs, const char *dirname, uint8_t levels, int &FilesCount) {
  size_t totalSize = 0;

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    _PRINT_DEBUG_("Failed to open directory: %s\n", dirname);
    return 0;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (levels > 0) {
        int subDirFiles = 0;                                                // Reset file count for each directory
        totalSize += getDirSize(fs, file.path(), levels - 1, subDirFiles);  // Recursive call for subdirectories
        FilesCount += subDirFiles;                                          // Add subdirectory file count to total
      }
    } else {
      totalSize += file.size();
      FilesCount += 1;
    }
    file = root.openNextFile();
  }

  return totalSize;
}


void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  _PRINT_DEBUG_("\n\nListing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    _PRINT_DEBUG_("Failed to open directory: %s\n", dirname);
    return;
  }

  size_t TotalSize = 0;
  int FilesCount = 0;

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      int DirFilesCount = 0;
      size_t dirSize = getDirSize(fs, file.path(), levels - 1, DirFilesCount);
      _PRINT_DEBUG_("SIZE: %s\t\tDIR : %s\t\tFILES: %d\n", formatBytes(dirSize).c_str(), file.name(), DirFilesCount);
      TotalSize += dirSize;
      FilesCount += DirFilesCount;
    } else {
      _PRINT_DEBUG_("SIZE: %s\t\tFILE: %s\n", formatBytes(file.size()).c_str(), file.name());
      TotalSize += file.size();
      FilesCount += 1;
    }
    file = root.openNextFile();
  }

  _PRINT_DEBUG_("\nTOTAL SIZE: %s\t\tFILES COUNT: %d\n\n", formatBytes(TotalSize).c_str(), FilesCount);
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


#ifdef SD_CARD
void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  } else if (path.endsWith(".mp3")) {
    dataType = "audio/mpeg";
  }

  File dataFile = SD.open(path.c_str());
  if (dataFile.isDirectory()) {
    path += "/edit.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile) {
    return false;
  }

  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    _PRINT_DEBUG_("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void handleFileUpload() {

  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (SD.exists((char *)upload.filename.c_str())) {
      SD.remove((char *)upload.filename.c_str());
    }
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    _PRINT_DEBUG_("Upload: START, filename: %s\n", upload.filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
    _PRINT_DEBUG_("Upload: WRITE, Bytes: %s\n", upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    _PRINT_DEBUG_("Upload: END, Size: %s\n", upload.totalSize);
  }
}

void deleteRecursive(String path) {
  File file = SD.open((char *)path.c_str());
  if (!file.isDirectory()) {
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) {
    File entry = file.openNextFile();
    if (!entry) {
      break;
    }
    String entryPath = path + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
  listDir(SD, "/", 0);
}

void handleCreate() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }

  if (path.indexOf('.') > 0) {
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if (file) {
      file.write(0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
  listDir(SD, "/", 0);
}

void printDirectory() {
  if (!server.hasArg("dir")) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg("dir");
  if (path != "/" && !SD.exists((char *)path.c_str())) {
    return returnFail("BAD PATH");
  }
  File dir = SD.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String output;
    if (cnt > 0) {
      output = ',';
    }

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.path();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
  }
  server.sendContent("]");
  dir.close();
}

void handleNotFound() {
  if (hasSD && loadFromSdCard(server.uri())) {
    return;
  }
  String message = "File not available or SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  _PRINT_DEBUG_(message.c_str());
}
#endif
