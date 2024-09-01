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


//esp32.menu.PartitionScheme.Booster=Audio (1.7MB OTA/0.5MB)
//esp32.menu.PartitionScheme.Booster.build.partitions=audio_spiffs
//esp32.menu.PartitionScheme.Booster.upload.maximum_size=1769472

//192.168.178.125/webserial


#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

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
#include <OneButton.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h> // SDA 4  SCL 5
#include <SFE_BMP180.h>
#include <Wire.h>
#include "SinricPro.h"
#include "SinricProTemperaturesensor.h"



#define TEMP_SENSOR_ID    "64df8a285e79b06c4b50167e"
#define APP_KEY           "f272c891-848d-4713-b9dd-26fc73f857c7"
#define APP_SECRET        "7297533d-4180-422d-893b-47d6a504c3a3-cd39a1c5-de41-4d8f-a10a-829c14a93bd6"


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
int mailCount = 0;
char Data[300];

int AudioVol = 8;


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
OneButton Indoor(INDOOR_PIN, false, true);
OneButton Outdoor(OUTDOOR_PIN, false, true);
Adafruit_AHTX0 aht;
SFE_BMP180 pressure;
SinricProTemperaturesensor &mySensor = SinricPro[TEMP_SENSOR_ID];

double humidity, temperature, lastTemperature, lastHumidity, T, P;
unsigned long lastEvent = (-60000); // last time event has been sent
char status;

//#define _PRINT_DEBUG_(msg) Serial.print(msg)
//#define _PRINT_DEBUG_(msg) WebSerial.print(msg)
#define _PRINT_DEBUG_(msg) { Serial.print(msg); WebSerial.print(msg); }

void SinricPro_Task(void *pvParameters ) {
  (void) pvParameters;
  vTaskDelay(5000);
  SinricPro.onConnected([]() {
    _PRINT_DEBUG_("Connected to SinricPro\r\n");
    //    for (int i = 0; i <= 3; i++) {
    //      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    //      delay(500);
    //    }
    //    digitalWrite(LED_BUILTIN, 1);
  });
  SinricPro.onDisconnected([]() {
    _PRINT_DEBUG_("Disconnected from SinricPro\r\n");
    //    for (int i = 0; i <= 7; i++) {
    //      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    //      delay(500);
    //    }
    //    digitalWrite(LED_BUILTIN, 1);
  });
  SinricPro.restoreDeviceStates(true); // Uncomment to restore the last known state from the server.
  SinricPro.begin(APP_KEY, APP_SECRET);

  _PRINT_DEBUG_("SinricPro Ready\n");

  if (!aht.begin() || !pressure.begin())
    _PRINT_DEBUG_("Could not find a valid AHT21 or BMP180, init fail\n");

  Indoor.attachLongPressStop(MailSensor_InDoor);
  //  Indoor.setPressMs(1000);
  Indoor.setDebounceMs(100);
  Outdoor.attachLongPressStop(MailSensor_OutDoor);
  //  Outdoor.setPressMs(1000);
  Outdoor.setDebounceMs(100);

  vTaskDelay(1);

  for (;;) {
    SinricPro.handle();
    Indoor.tick();
    Outdoor.tick();
    handleTemperature_Pressure_sensor();
    vTaskDelay(5);
  }
}

void handleTemperature_Pressure_sensor() {
  if (!aht.begin() || !pressure.begin()) {
    _PRINT_DEBUG_("--Could not find a valid AHT21 or BMP180, init fail\n");
    delay(1000);
    return;
  }

  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);

  humidity = hum.relative_humidity + 9.0;
  temperature = temp.temperature - 5.0;

  status = pressure.startTemperature();
  if (status != 0) {
    delay(status);
    status = pressure.getTemperature(T);
    if (status != 0) {
      status = pressure.startPressure(3);
      if (status != 0) {
        delay(status);
        status = pressure.getPressure(P, T);
        if (status != 0) {
          snprintf(tempBuffer, sizeof(tempBuffer), "Temperature: %2.1f Celsius\tPressure: %fmB\r\n", T, P);
          _PRINT_DEBUG_(tempBuffer);
        } else _PRINT_DEBUG_("error retrieving pressure measurement\n");
      } else _PRINT_DEBUG_("error starting pressure measurement\n");
    } else _PRINT_DEBUG_("error retrieving temperature measurement\n");
  } else _PRINT_DEBUG_("error starting temperature measurement\n");


  if (isnan(temperature) || isnan(humidity)) { // reading failed...
    _PRINT_DEBUG_("AHT reading failed!\r\n");  // print error message
    return;                                    // try again next time
  }


  unsigned long actualMillis = millis();
  if (actualMillis - lastEvent < 60000) return; //only check every EVENT_WAIT_TIME milliseconds
  if (temperature == lastTemperature || humidity == lastHumidity) return;

  SinricProTemperaturesensor &mySensor = SinricPro[TEMP_SENSOR_ID];  // get temperaturesensor device
  bool success = mySensor.sendTemperatureEvent(temperature, humidity); // send event

  if (success) {
    snprintf(tempBuffer, sizeof(tempBuffer), "Temperature: %2.1f Celsius\tHumidity: %2.1f%%\r\n", temperature, humidity);
    _PRINT_DEBUG_(tempBuffer);
  } else {
    _PRINT_DEBUG_("Something went wrong...could not send Event to server!\r\n");
  }

  lastTemperature = temperature;  // save actual temperature for next compare
  lastHumidity = humidity;        // save actual humidity for next compare
  lastEvent = actualMillis;       // save actual time for next compare

}

void MailSensor_OutDoor() {
  mailCount = mailCount + 1;
  snprintf(tempBuffer, sizeof(tempBuffer), "You got %d Mails", mailCount);
  _PRINT_DEBUG_(tempBuffer);
  //  Callmebot.facebookMessage(apiKey2, (String)buff);
  //  Callmebot.whatsappMessage(phoneNumber, apiKey, (String)buff);
}

void MailSensor_InDoor() {
  mailCount = 0;
  //  Callmebot.facebookMessage(apiKey2, "Mails Cleared");
  //  Callmebot.whatsappMessage(phoneNumber, apiKey, "Mails Cleared");
  _PRINT_DEBUG_("Mails Cleared");
}


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

  snprintf(tempBuffer, sizeof(tempBuffer), "%02d.%02d.%04d  %02d:%02d:%02d\n",
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
      if (i == 0 || i == 4) audio.setVolume(4);
      else audio.setVolume(AudioVol);
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

  snprintf(tempBuffer, sizeof(tempBuffer), "%02d.%02d.%04d  %02d:%02d:%02d  ",
           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  _PRINT_DEBUG_(tempBuffer);

  snprintf(tempBuffer, sizeof(tempBuffer), "%s-%s  F%s D%s A%s M%s I%s\n\n",
           Date_hijri.c_str(), Month_hijri.c_str(), Time_Fajr.c_str(), Time_Dhuhr.c_str(),
           Time_Asr.c_str(), Time_Maghrib.c_str(), Time_Isha.c_str());
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
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
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

void handleNotFound(AsyncWebServerRequest *request) {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += request->params();
  message += "\n";
  for (uint8_t i = 0; i < request->params(); i++) {
    AsyncWebParameter* p = request->getParam(i);
    message += " " + p->name() + ": " + p->value() + "\n";
  }
  request->send(404, "text/plain", message);
}


void handleRoot(AsyncWebServerRequest *request) {
  String html = R"=====(
<html>
<head>
    <style>
        body {
            background-color: black;
            color: white;
            font-family: Arial, sans-serif;
            margin: 0;
            height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            flex-direction: column;
            line-height: 1.6; /* Improve readability */
        }

        .data-container {
            background: rgba(255, 255, 255, 0.1); /* Semi-transparent background */
            border-radius: 5px; /* Rounded corners */
            padding: 15px; /* Inner space */
            margin-bottom: 20px; /* Space between containers */
            width: 50%; /* Take 80% of parent's width */
            text-align: center; /* Center the text inside */
        }

        .data-title {
            font-size: 1.5em; /* Make the title slightly larger */
            font-weight: bold; /* Bold title text */
            margin-bottom: 10px; /* Space between title and value */
        }

        .data-value {
            font-size: 1.2em; /* Make the value even larger */
        }
    </style>
    <script>
        function fetchData() {
            fetch('/sensordata')
                .then(response => response.text())
                .then(data => {
                    let lines = data.split('\n');
                    document.getElementById('AHT21_Temperature1').innerText = lines[0];
                    document.getElementById('AHT21_Humidity').innerText = lines[1];
                    document.getElementById('BMP180_Temperature2').innerText = lines[2];
                    document.getElementById('BMP180_Pressure').innerText = lines[3];
                    document.getElementById('Mails').innerText = lines[4];
                });
        }
        setInterval(fetchData, 1000);
    </script>
</head>

<body onload="fetchData()">
    <div class="data-container">
        <div class="data-title">AHT21 Temp 1:</div>
        <div class="data-value" id="AHT21_Temperature1">Loading AHT21 Temperature...</div>
    </div>
    <div class="data-container">
        <div class="data-title">AHT21 Humidity:</div>
        <div class="data-value" id="AHT21_Humidity">Loading AHT21 Humidity...</div>
    </div>
    <div class="data-container">
        <div class="data-title">BMP180 Temp 2:</div>
        <div class="data-value" id="BMP180_Temperature2">Loading BMP180 Temperature...</div>
    </div>
    <div class="data-container">
        <div class="data-title">BMP180 Pressure:</div>
        <div class="data-value" id="BMP180_Pressure">Loading BMP180 Pressure...</div>
    </div>
    <div class="data-container">
        <div class="data-title">Mails:</div>
        <div class="data-value" id="Mails">Loading mail count...</div>
    </div>
</body>
</html>

  )=====";
  request->send(200, "text/html", html);
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

  if (!MDNS.begin(Name)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder Ready\n");

  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  /* Attach Message Callback */
  WebSerial.onMessage(recvMsg);
  server.begin();
  Serial.print("WebSerial Ready\n");

  server.on("/sensordata", HTTP_GET, [](AsyncWebServerRequest * request) {
    sprintf(Data, "%2.1f Celsius\n%2.1f%%\n%2.1f Celsius\n%2.2f Bar\n", temperature, humidity, T - 5, P / 1000.0);
    request->send(200, "text/plain", Data);
  });
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  MDNS.addService("http", "tcp", 80);

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
  xTaskCreatePinnedToCore(SinricPro_Task, "SinricPro_Task", 8 * 1024, NULL, 3, NULL, 1);
}

void loop() {
  ArduinoOTA.handle();
}
