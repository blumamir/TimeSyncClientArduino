#pragma once

#include <Stream.h> // due to https://github.com/espressif/arduino-esp32/issues/2872
#include "AsyncUDP.h"

class TimeSync {

public:

  TimeSync();

  void setup(const IPAddress &ntpServerAddress, uint8_t ntpServerPort);
  void loop();

private:
  void sendNTPpacket();
  void updateLimits(unsigned long currMillis);

// network config values
private:
  IPAddress m_address;
  uint8_t m_ntpServerPort;

private:
  void onNtpPacketCallback(AsyncUDPPacket &packet);

public:

  // this is a heuristic time for tuning the update algorithm.
  // user should configure the time frequency in ms, in which clock updates
  // are desired to take place.
  // the library will use this value to adapt the thresholds, so time will be
  // updated at around this time, while using the resources
  // (cpu, network, server) as little as possible.
  // there is no practial way for the library to ensure that time will update
  // at this frequency (or any other frequency). this value is just for
  // best effort
  unsigned int m_desirableUpdateFreqMs = 1000 * 60 * 10; // 10 minutes

  // bottom limit for ntp packet send to server.
  // the library will not send packets at rate higher than that
  unsigned int m_minServerSendTimeMs = 500; // half a second

  // top limit for ntp packet send to server.
  // the library will not wait more than this time for an update packet to server
  unsigned int m_maxServerSendTimeMs = 1000 * 60 * 2; // two minutes

  // top limit for allowed round trip time in worst case
  // the library will not use a value if the round trip time is larger than
  // this number of ms
  unsigned int m_maxAllowedRoundTripMs = 15;

// timesync algorithm
private:

  unsigned long m_limitRoundtripForUpdate = m_maxAllowedRoundTripMs;
  unsigned long m_timeBetweenSendsMs = m_minServerSendTimeMs;

  // time at which we sent last ntp packet
  uint32_t m_lastNtpSendTime = 0;
  bool m_lastNtpPacketConsumed = false;

  // time in esp millis() of when we last updated the clock from ntp server
  uint32_t m_lastClockUpdateTime = 0;
  uint32_t m_lastRoundTripTimeMs = 0;

public:
  bool m_isTimeValid = false;
  unsigned long m_startTimeSec = 0;
  unsigned long m_startTimeMillis = 0;
  // espStartTime is the ms since epoch of the time that esp started (millis() function return 0)
  // so if you have the current esp millis() value, you can add it to this value to get current ms since ephoc
  int64_t m_espStartTimeMs = 0;

private:

  static const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
  uint8_t m_packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

  AsyncUDP m_udp;
};

