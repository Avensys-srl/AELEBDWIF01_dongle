#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_task(void *arg);
void publish_debug_message(const uint8_t *data, size_t data_len, const char *topic, const char *address);
void mqtt_subscribe_app_topics(const char *address);

extern esp_mqtt_client_handle_t client;
extern bool is_mqtt_ready;

#ifdef __cplusplus
}
#endif

#endif
