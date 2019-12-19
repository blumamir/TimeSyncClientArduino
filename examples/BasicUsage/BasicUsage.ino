#include <WiFi.h>

#include <TimeSync.hpp>

TimeSync::TimeSyncClient timesync;

// replace the * with the values in your setup
const char *ssid = "****";
const char *password =  "****";
const char *ntpServerIpStr = "*.*.*.*";

void print_uint64_t(uint64_t num) {

  char rev[128]; 
  char *p = rev+1;

  while (num > 0) {
    *p++ = '0' + ( num % 10);
    num/= 10;
  }
  p--;
  /*Print the number which is now in reverse*/
  while (p > rev) {
    Serial.print(*p--);
  }
}

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print("Connecting to WiFi "); Serial.print(ssid); Serial.println("..");
    }

    Serial.println("Connected to the WiFi network");

    IPAddress ntpServerIp;
    ntpServerIp.fromString(ntpServerIpStr);
    timesync.updateConfiguration(15, 1000 * 60 * 10, 250, 1000 * 60 * 2);
    timesync.setup(ntpServerIp, 12321);
}

unsigned int lastPrintTime = 0;

void loop()
{
  bool isTimeChanged;  
  timesync.loop(&isTimeChanged);
  if(isTimeChanged)
  {
    Serial.println("TIME CHANGED. new synced clock is availible to the esp");
  }

  if(millis() - lastPrintTime > 5000)
  {
      lastPrintTime = millis();
      Serial.print(millis());
      Serial.print(": time is valid: ");
      if(timesync.isTimeValid())
      {
          Serial.print("yes. esp started when the TimeSyncServer epoch time showed: ");
          print_uint64_t(timesync.getEspStartTimeMs());
          Serial.println("");
      }
      else
      {
          Serial.println("no");
      }
  }
  delay(10); // simulate some other work done in the loop
}