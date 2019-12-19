#include "TimeSync.hpp"

#include "Arduino.h" // for millis() function

#ifdef TIME_SYNC_DEBUG
void print_uint64(uint64_t num) {

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
#endif // TIME_SYNC_DEBUG

TimeSync::TimeSync() {

  // prepare the time request buffer for useage
  memset(m_requestTimeMsgBuffer, 0, REQUEST_TIME_PACKET_SIZE);
  // Initialize values needed to form NTP request
  m_requestTimeMsgBuffer[0] = 'T';
  m_requestTimeMsgBuffer[1] = 'S'; 
  m_requestTimeMsgBuffer[2] = 'P'; 
  m_requestTimeMsgBuffer[3] = 1; // Protocol Version
}

void TimeSync::sendTspPacket() {

  // stamp the send time
  m_lastTspSendTime = millis();

  // generate request cookie which is the esp millis() value
  m_lastTspReqCookie = m_lastTspSendTime;
  *((uint64_t *)(m_requestTimeMsgBuffer + 8)) = m_lastTspReqCookie;

  // send the buffer to lwip socket
  pbuf* pb = pbuf_alloc(PBUF_TRANSPORT, REQUEST_TIME_PACKET_SIZE, PBUF_RAM);
  if(pb == NULL) {
    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: failed to allocate memory for packet send");
    #endif // TIME_SYNC_DEBUG
    return;
  }

  uint8_t* pbufPayloadPtr = reinterpret_cast<uint8_t*>(pb->payload);
  memcpy(pbufPayloadPtr, m_requestTimeMsgBuffer, REQUEST_TIME_PACKET_SIZE);

  // we need to call lwip api from the right core. this code handles this task.
  // TODO: can it be done better? what is `tcpip_api_call` actually doing?
  UdpSendData udpSendData;
  udpSendData.pcb = m_lwipPcb;
  udpSendData.pb = pb;
  tcpip_api_call(lwipSend, (struct tcpip_api_call_data*)&udpSendData);

  if(udpSendData.err != ERR_OK)
  {
    // TODO: how to handle error in send?

    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: error in sending time request to server");
    #endif // TIME_SYNC_DEBUG
  }
  else {
    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: sending time sync request packet to server");
    #endif // TIME_SYNC_DEBUG
  }

  pbuf_free(pb);
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

void TimeSync::handleTspResponseData(const UdpTimeResponseData &udpTimeResponseData)
{
  if(udpTimeResponseData.responseCookie != m_lastTspReqCookie) {
    // this is a reponse for some old request, or not an tsp packet

    #ifdef TIME_SYNC_DEBUG
    Serial.print("TimeSync: ignoring tsp response. expected cookie: "); print_uint64(m_lastTspReqCookie); Serial.print(" and got "); print_uint64(udpTimeResponseData.responseCookie); Serial.println("");
    #endif // TIME_SYNC_DEBUG

    return;
  }
  m_lastTspReqCookie = 0; // mark 0 means we consumed the response

  // check if time update is needed
  unsigned int roundTrip = udpTimeResponseData.espPacketRecvTime - m_lastTspSendTime;
  if(roundTrip >= m_limitRoundtripForUpdate) {
    // this packet took too much time for round trip. we don't use it

    #ifdef TIME_SYNC_DEBUG
  Serial.print("TimeSync: round trip is "); Serial.print(roundTrip); Serial.print(" >= "); Serial.print(m_limitRoundtripForUpdate); Serial.println(" ms, NOT updating internal time");
    #endif // TIME_SYNC_DEBUG

    return;
  }

  #ifdef TIME_SYNC_DEBUG
  Serial.print("TimeSync: round trip is "); Serial.print(roundTrip); Serial.print(" < "); Serial.print(m_limitRoundtripForUpdate); Serial.println(" ms, updating internal time");
  #endif // TIME_SYNC_DEBUG

  // approximate the time esp showed (miilis()) when the server stampped the ephoc time.
  unsigned int espTimeWhenServerStampped = udpTimeResponseData.espPacketRecvTime - (roundTrip / 2); 

  m_lastClockUpdateTime = udpTimeResponseData.espPacketRecvTime;
  m_lastRoundTripTimeMs = roundTrip;
  m_isTimeValid = true;
  m_espStartTimeMs = udpTimeResponseData.epochTimeFromServer - espTimeWhenServerStampped;

  updateLimits(m_lastClockUpdateTime);
}

void TimeSync::setup(const IPAddress &ntpServerAddress, uint16_t tspServerPort) {

  m_address = ntpServerAddress;
  m_tspServerPort = tspServerPort;

  m_responsesQueue = xQueueCreate(4, sizeof(UdpTimeResponseData));
  if(!m_responsesQueue)
  {
    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: queue create failed");
    #endif // TIME_SYNC_DEBUG
    return;
  }

  m_lwipPcb = udp_new();

  // tell lwip to call this function when a udp packet arrive on this socket (pcb)
  udp_recv(m_lwipPcb, &TimeSync::lwipUdpRecvCallback, (void *)this);

  ip_addr_t lwipIpv4Addr;
  lwipIpv4Addr.type = IPADDR_TYPE_V4;
  lwipIpv4Addr.u_addr.ip4.addr = ntpServerAddress;

  UdpConnectData udpConnectData;
  udpConnectData.pcb = m_lwipPcb;
  udpConnectData.addr = &lwipIpv4Addr;
  udpConnectData.port = tspServerPort;
  tcpip_api_call(lwipConnect, (struct tcpip_api_call_data*)&udpConnectData);

  if(udpConnectData.err != ERR_OK)
  {
      // TODO - handle error
      return;
  }

  updateLimits(0);
}

void TimeSync::consumeResponsesFromQueue()
{
  UdpTimeResponseData udpTimeResponseData;
  while(xQueueReceive(m_responsesQueue, &udpTimeResponseData, 0) == pdTRUE)
  {
    handleTspResponseData(udpTimeResponseData);
  }
}


void TimeSync::loop() {

  consumeResponsesFromQueue();

  unsigned long currMillis = millis();
  if( (currMillis - m_lastTspSendTime) > m_timeBetweenSendsMs) {
    sendTspPacket();
    updateLimits(currMillis);
  }
}

void TimeSync::updateConfiguration(
    unsigned int maxAllowedRoundTripMs,
    unsigned int desirableUpdateFreqMs,
    unsigned int minServerSendTimeMs,
    unsigned int maxServerSendTimeMs
  )
{
  m_maxAllowedRoundTripMs = maxAllowedRoundTripMs > 0 ? maxAllowedRoundTripMs : defaultMaxAllowedRoundTripMs;
  m_desirableUpdateFreqMs = desirableUpdateFreqMs > 0 ? desirableUpdateFreqMs : defaultDesirableUpdateFreqMs;
  m_minServerSendTimeMs = minServerSendTimeMs > 0 ? minServerSendTimeMs : defaultMinServerSendTimeMs;
  m_maxServerSendTimeMs = maxServerSendTimeMs > 0 ? maxServerSendTimeMs : defaultMaxAllowedRoundTripMs;  
}

void TimeSync::handlePbufOnLwipContext(pbuf *pb)
{
  if(pb->len < 24)
  {
    #ifdef TIME_SYNC_DEBUG
    Serial.print("TimeSync: ignoring tsp response. packet size should be 24, found: "); Serial.println(pb->len);
    #endif // TIME_SYNC_DEBUG

    return;
  }

  uint8_t *packetResponseBuffer = (uint8_t *)pb->payload;

  // validate that this packet is ineeded from the TSP protocol
  if( *(packetResponseBuffer + 0) != 'T' ||
      *(packetResponseBuffer + 1) != 'S' ||
      *(packetResponseBuffer + 2) != 'P')
  {
    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: ignoring tsp response. TSP header not valid. probably wrong packet arrived to socket");
    #endif // TIME_SYNC_DEBUG

    return;
  }

  // prepare message for queue. 
  // we copy everything we need from the udp buffer so we don't need it anymore
  UdpTimeResponseData udpTimeResponseData;
  udpTimeResponseData.espPacketRecvTime = millis(); 
  udpTimeResponseData.responseCookie = *((uint64_t *)(packetResponseBuffer + 8));
  udpTimeResponseData.epochTimeFromServer = *((uint64_t *)(packetResponseBuffer + 16));

  // it is OK to access m_responsesQueue from this context (no need for synchronization)
  if (xQueueSend(m_responsesQueue, &udpTimeResponseData, 0) != pdTRUE) {
    #ifdef TIME_SYNC_DEBUG
    Serial.println("TimeSync: ignoring tsp response. cannot send it on queue ");
    #endif // TIME_SYNC_DEBUG
  }
}

err_t TimeSync::lwipSend(struct tcpip_api_call_data *data){
    UdpSendData *msg = (UdpSendData *)data;
    msg->err = udp_send(msg->pcb, msg->pb);
    return msg->err;
}

err_t TimeSync::lwipConnect(struct tcpip_api_call_data *data){
    UdpConnectData *msg = (UdpConnectData *)data;
    msg->err = udp_connect(msg->pcb, msg->addr, msg->port);
    return msg->err;
}

void TimeSync::lwipUdpRecvCallback(void *arg, udp_pcb *pcb, pbuf *pb, const ip_addr_t *addr, uint16_t port)
{
  TimeSync *senderTimeSync = reinterpret_cast<TimeSync*>(arg);

  while(pb != NULL) {

      pbuf * currPb = pb;
      pb = pb->next;
      currPb->next = NULL;

      senderTimeSync->handlePbufOnLwipContext(currPb);

      pbuf_free(currPb);
  }
}
