#pragma once

#include <IPAddress.h>

#include "lwip/udp.h"
#include "lwip/priv/tcpip_priv.h"

namespace TimeSync
{

// The following structs are used to move messages accross cores, 
// so operations are handled on the right context

typedef struct
{
  uint32_t espPacketRecvTime; 
  uint64_t responseCookie;
  uint64_t epochTimeFromServer;
} UdpTimeResponseData;

typedef struct
{
    struct tcpip_api_call_data call;
    udp_pcb * pcb;
    struct pbuf *pb;
    err_t err;
} UdpSendData;

typedef struct
{
    struct tcpip_api_call_data call;
    udp_pcb * pcb;
    const ip_addr_t *addr;
    uint16_t port;
    err_t err;
} UdpConnectData;


class TimeSyncClient {

public:

  TimeSyncClient();

  void setup(const IPAddress &ntpServerAddress, uint16_t tspServerPort);
  void loop();

public:
  bool isTimeValid() { return m_isTimeValid; }
  int64_t getEspStartTimeMs() { return m_espStartTimeMs; }
  int64_t getCurrentEpochTimeMs(unsigned long currentEspMillis) { return (m_espStartTimeMs + (int64_t)currentEspMillis); }

public:
  void updateConfiguration(
    unsigned int maxAllowedRoundTripMs,
    unsigned int desirableUpdateFreqMs,
    unsigned int minServerSendTimeMs,
    unsigned int maxServerSendTimeMs
  );

private:
  void sendTspPacket();
  void updateLimits(unsigned long currMillis);

private:
  // NOTICE! this function is called from lwip callback, and should not access 
  // class members without proper thought on synchronization
  void handlePbufOnLwipContext(pbuf *pb);

// lwip api static functions
private:
  static err_t lwipSend(struct tcpip_api_call_data *data);
  static err_t lwipConnect(struct tcpip_api_call_data *data);
  static void lwipUdpRecvCallback(void *arg, udp_pcb *pcb, pbuf *pb, const ip_addr_t *addr, uint16_t port);

// network config values
private:
  IPAddress m_address;
  uint16_t m_tspServerPort;

private:
  void consumeResponsesFromQueue();
  void handleTspResponseData(const UdpTimeResponseData &packet);

public:

  // top limit for allowed round trip time in worst case
  // the library will not use a value if the round trip time is larger than
  // this number of ms
  static const unsigned int defaultMaxAllowedRoundTripMs = 15;
  unsigned int m_maxAllowedRoundTripMs = defaultMaxAllowedRoundTripMs;

  // this is a heuristic time for tuning the update algorithm.
  // user should configure the time frequency in ms, in which clock updates
  // are desired to take place.
  // the library will use this value to adapt the thresholds, so time will be
  // updated at around this time, while using the resources
  // (cpu, network, server) as little as possible.
  // there is no practial way for the library to ensure that time will update
  // at this frequency (or any other frequency). this value is just for
  // best effort
  static const unsigned int defaultDesirableUpdateFreqMs = 1000 * 60 * 10; // 10 minutes
  unsigned int m_desirableUpdateFreqMs = defaultDesirableUpdateFreqMs;

  // bottom limit for ntp packet send to server.
  // the library will not send packets at rate higher than that
  static const unsigned int defaultMinServerSendTimeMs = 500; // half a second
  unsigned int m_minServerSendTimeMs = defaultMinServerSendTimeMs; 

  // top limit for ntp packet send to server.
  // the library will not wait more than this time for an update packet to server
  static const unsigned int defaultMaxServerSendTimeMs = 1000 * 60 * 2; // two minutes
  unsigned int m_maxServerSendTimeMs = defaultMaxServerSendTimeMs;

// timesync algorithm
private:

  unsigned long m_limitRoundtripForUpdate = m_maxAllowedRoundTripMs;
  unsigned long m_timeBetweenSendsMs = m_minServerSendTimeMs;

  // millis at which we sent time request packet
  uint32_t m_lastTspSendTime = 0;
  uint64_t m_lastTspReqCookie = 0; // 0 is invalid, != 0 is valid cookie

  // time in esp millis() of when we last updated the clock from ntp server
  uint32_t m_lastClockUpdateTime = 0;
  uint32_t m_lastRoundTripTimeMs = 0;

private:
  bool m_isTimeValid = false;
  // espStartTime is the ms since epoch of the time that esp started (millis() function return 0)
  // so if you have the current esp millis() value, you can add it to this value to get current ms since epoch
  int64_t m_espStartTimeMs = 0;

private:

  static const int REQUEST_TIME_PACKET_SIZE = 16;
  uint8_t m_requestTimeMsgBuffer[REQUEST_TIME_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

  // internal header for the socket, used with lwip api
  udp_pcb *m_lwipPcb = NULL;
  xQueueHandle m_responsesQueue;
};

}