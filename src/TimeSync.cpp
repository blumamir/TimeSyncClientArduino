#include "TimeSync.hpp"
#include "Arduino.h" // for millis() function

TimeSync::TimeSync() {
  // set all bytes in the buffer to 0
  memset(m_packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  m_packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  m_packetBuffer[1] = 0;     // Stratum, or type of clock
  m_packetBuffer[2] = 6;     // Polling Interval
  m_packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  m_packetBuffer[12]  = 49;
  m_packetBuffer[13]  = 0x4E;
  m_packetBuffer[14]  = 49;
  m_packetBuffer[15]  = 52;
}

void TimeSync::sendNTPpacket() {

  m_lastNtpSendTime = millis();
  m_lastNtpPacketConsumed = true;

  // all NTP fields have been given values, now
  // we can send a packet requesting a timestamp:
  m_udp.writeTo(m_packetBuffer, NTP_PACKET_SIZE, m_address, m_ntpServerPort);

  //Serial.println("sendingPacket");
}

void TimeSync::updateLimits(unsigned long currMillis) {

  // don't update the limits if we didn't have any updates.
  // we cannot read valid value from m_lastClockUpdateTime in that case
  if(!m_isTimeValid) {

    // when library starts, time is not known yet, so we need to fetch it ASAP
    m_timeBetweenSendsMs = m_minServerSendTimeMs;
    // and we want any value we can get just to start, we will improve it later
    m_limitRoundtripForUpdate = m_maxAllowedRoundTripMs;

    return;
  }

  //Serial.println();Serial.println("******************");


  // we will calculate two value that derive a quality of the clock we got.
  // both of these times are in scale 0.0 to 1.0, where 0.0 means that the
  // value is most bad, and 1.0 means the value is most good.
  // 0.0 -> bad, 1.0 -> good

  // first value is time since last update. if we just got a clock read,
  // then we can wait some time before the next one, and not bother the network.
  // if last clock read was long ago, we want the next one to happen soon.
  // if the value is at relTimeSinceLastUpdate, we want clock read ASAP
  unsigned int timeSinceLastUpdate = currMillis - m_lastClockUpdateTime;
  float relTimeSinceLastUpdate = max(0.0f, 1.0f - (float)timeSinceLastUpdate / (float) m_desirableUpdateFreqMs);
  //Serial.print("relTimeSinceLastUpdate = "); Serial.println(relTimeSinceLastUpdate);

  // second value is the last round trip time. if we had small round trip time,
  // we can delay the next request since we have good value.
  // if the last round trip time was large (close to the limit), then we want
  // to request for a new value ASAP
  float relRoundTrip = max(0.0f, 1.0f - (float)m_lastRoundTripTimeMs / (float)m_maxAllowedRoundTripMs);
  //Serial.print("relRoundTrip = "); Serial.println(relRoundTrip);

  // now we need to combine these two values.
  // we need to 'OR' them -> if at least one of them has low (bad) value,
  // then next request should happen fast. if both has good values,
  // then we can delay next request for later time.
  // so we multiply those two value to get the desired effect
  float combinedFactor = min(1.0f, relTimeSinceLastUpdate * relRoundTrip);
  //Serial.print("combinedFactor = "); Serial.println(combinedFactor);

  // calculate m_timeBetweenSendsMs
  // if combinedFactor == 1.0, means we are not in hurry -> take max limits
  // if combinedFactor == 0.0, means we need to update ASAP -> use min limits
  // values in between are change linearly.
  // this is OK since when we next send packet we will calculate them again
  // anyway, so we just need a proper value.
  uint32_t diffBetweenLimits = m_maxServerSendTimeMs - m_minServerSendTimeMs;
  m_timeBetweenSendsMs = m_minServerSendTimeMs + (uint32_t)(diffBetweenLimits * combinedFactor);
  //Serial.print("m_timeBetweenSendsMs = "); Serial.println(m_timeBetweenSendsMs);

  // calculate m_limitRoundtripForUpdate
  // if combinedFactor >= 1.0, means we are not in hurry -> use only small RTT
  // if combinedFactor == 0.0, means we need to update ASAP -> use max RTT
  // for values in between -> linearly
  m_limitRoundtripForUpdate = m_maxAllowedRoundTripMs - (uint32_t)(m_maxAllowedRoundTripMs * combinedFactor);
  //Serial.print("m_limitRoundtripForUpdate = "); Serial.println(m_limitRoundtripForUpdate);

  //Serial.println("******************"); Serial.println();
}

void TimeSync::onNtpPacketCallback(AsyncUDPPacket &packet)
{
  // this might be a retransmission of a packet we already received,
  // or some other network issue which we cannot handle
  if(!m_lastNtpPacketConsumed) {
    return;
  }

  // stamp the recv time, this is important to be done ASAP
  uint32_t recvTime = millis();

  // check if time update is needed
  unsigned int roundTrip = recvTime - m_lastNtpSendTime;
  m_lastNtpPacketConsumed = false;
  if(roundTrip >= m_limitRoundtripForUpdate) {
    // this packet took too much time for round trip. we don't use it
    //Serial.print("===> round trip is "); Serial.print(roundTrip); Serial.println(" ms, not updating internal time");
    return;
  }

  Serial.print("===> round trip is "); Serial.print(roundTrip); Serial.println(" ms, updating internal time");

  // parse ntp response buffer
  uint8_t *packetBuffer = packet.data();

  // seconds part
  uint32_t highWord = word(packetBuffer[40], packetBuffer[41]);
  uint32_t lowWord = word(packetBuffer[42], packetBuffer[43]);
  uint32_t secFromNtpEpoch = highWord << 16 | lowWord;
  // ms part
  uint32_t otherHighWord = word(packetBuffer[44], packetBuffer[45]);
  uint32_t otherLowWord = word(packetBuffer[46], packetBuffer[47]);
  uint32_t fractional = otherHighWord << 16 | otherLowWord;
  float readMsF = ((float)fractional)*2.3283064365387E-07; // fractional*(1000/2^32) to get milliseconds.
  if(readMsF < 0.0) {
    // should not happen, but we will check just in case
    readMsF = 0.0;
  }
  // this is just the fractional part. should be in range [0, 1000)
  uint32_t msPart = (uint32_t)(readMsF);
  if(msPart >= 1000) {
    // this can happen in very rare scenarions, because float calculations are
    // not precise and can inject errors
    msPart = 999;
  }

  // Unix time starts on Jan 1 1970. ntp epoch is Jan 1 1900,
  // In seconds, that's 2208988800:
  static const unsigned long seventyYears = 2208988800UL;
  uint32_t secFromEpoch = secFromNtpEpoch - seventyYears;

  // esp epoch is the time when esp started. it is the time for which millis()
  // function returned 0.
  // now we will calculate what was the time (seconds + ms) on esp epoch.
  // that is done by reducing 'recvTime' from the time sent from ntp server.

  unsigned long recvTimeSec = recvTime / 1000;
  unsigned long recvTimeMillis = recvTime % 1000;
  unsigned long startTimeSec = secFromEpoch - recvTimeSec;
  unsigned long startTimeMillis;
  if (((int32_t)msPart - (int32_t)recvTimeMillis) < 0) {
    startTimeMillis = 1000 - (recvTimeMillis - msPart);
    startTimeSec--;
  }
  else {
    startTimeMillis = msPart - recvTimeMillis;
  }
  m_lastClockUpdateTime = recvTime;
  m_lastRoundTripTimeMs = roundTrip;
  m_isTimeValid = true;
  m_espStartTimeMs = (uint64_t)(((uint64_t)startTimeSec)*1000 + (uint64_t)startTimeMillis);

  updateLimits(m_lastClockUpdateTime);
}

void TimeSync::setup(const IPAddress &ntpServerAddress, uint8_t ntpServerPort) {

  m_address = ntpServerAddress;
  m_ntpServerPort = ntpServerPort;

  if(m_udp.connect(m_address, m_ntpServerPort)) {
    Serial.println("UDP connected");
    AuPacketHandlerFunction callback = std::bind(&TimeSync::onNtpPacketCallback, this, std::placeholders::_1);
    m_udp.onPacket(callback);
  }

  updateLimits(0);
}

void TimeSync::loop() {
  unsigned long currMillis = millis();
  if( (currMillis - m_lastNtpSendTime) > m_timeBetweenSendsMs) {
    sendNTPpacket();
    updateLimits(currMillis);
  }
}

void TimeSync::UpdateConfiguration(
    unsigned int maxAllowedRoundTripMs,
    unsigned int desirableUpdateFreqMs,
    unsigned int minServerSendTimeMs,
    unsigned int maxServerSendTimeMs
  )
{
  m_maxAllowedRoundTripMs = maxAllowedRoundTripMs > 0 ? maxServerSendTimeMs : defaultMaxAllowedRoundTripMs;
  m_desirableUpdateFreqMs = desirableUpdateFreqMs > 0 ? desirableUpdateFreqMs : defaultDesirableUpdateFreqMs;
  m_minServerSendTimeMs = minServerSendTimeMs > 0 ? minServerSendTimeMs : defaultMinServerSendTimeMs;
  m_maxServerSendTimeMs = maxServerSendTimeMs > 0 ? maxAllowedRoundTripMs : defaultMaxAllowedRoundTripMs;  
}

