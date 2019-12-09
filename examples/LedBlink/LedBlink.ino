/*
This test will blink the ESP32 onboard led.
If you use multiple bobards which all sync to the same server,
they should all blink together, which proves (to some extent) 
that they have a synchronized time over the network.
*/


#include <WiFi.h>

#include <TimeSync.hpp>

#define LED_GPIO 2

TimeSync timesync;

// replace the * with the values in your setup
const char *ssid = "****";
const char *password =  "****";
const char *ntpServerIpStr = "*.*.*.*";

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

    pinMode(LED_GPIO, OUTPUT);
}

unsigned int lastPrintTime = 0;

void loop()
{
    timesync.loop();
    if(timesync.isTimeValid())
    {
        int64_t epochTime = timesync.getCurrentEpochTimeMs(millis());
        int msPart = epochTime % 1000;
        int secondPart = (epochTime % (60 * 1000)) / 1000;

        // use two patterns to make synchronization more obvious.
        // one per second, and other per 10 seconds
        if(secondPart % 10 != 0)
        {
            // blink for 50 ms every second
            if(msPart < 50)
            {
                digitalWrite(LED_GPIO, HIGH);
            }
            else
            {
                digitalWrite(LED_GPIO, LOW);
            }
        }
        else
        {
            // blink 50 ms on and 50 ms off for the entire second
            if( (msPart % 100) < 50)
            {
                digitalWrite(LED_GPIO, HIGH);
            }
            else
            {
                digitalWrite(LED_GPIO, LOW);
            }
        }
    }
    delay(10); // simulate some other work done in the loop
}