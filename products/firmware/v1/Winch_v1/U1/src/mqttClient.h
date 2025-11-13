#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include "globals.h"

// MQTTコールバック関数
void mqttCallback(char* topic, byte* payload, unsigned int length);

// MQTT再接続関数
void reconnect();

// ホーミングポーリング関数
bool pollHoming(uint16_t every_ms);

#endif
