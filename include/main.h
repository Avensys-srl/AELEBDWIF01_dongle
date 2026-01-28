#ifndef MAIN_H
#define MAIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_mac.h"

//#include "sys_uart.h"
#include "wifi_connect.h"
#include "Uart1.h"
#include "gpio_api.h"
#include "eeprom_data_struct.h"

#define NVS_NAMESPACE "storage"

#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64

extern char WIFI_SSID[MAX_SSID_LENGTH + 1];
extern char WIFI_PASSWORD[MAX_PASSWORD_LENGTH + 1];

extern bool connect_to_wifi;
extern bool wifi_is_ssid_send;
extern bool wifi_is_pass_send;

extern esp_mqtt_client_handle_t client;
extern bool                     read_eeprom_data;
extern bool                     is_mqtt_ready;
extern bool                     send_eeprom_read_request;

typedef  enum{
	WBM_initialize = 0,
	WBM_Communicating,
	WBM_Connected,
	WBM_Error
} _WBM_Com_State;


typedef struct {
	uint8_t Start_Adress;
	uint8_t Count;
	bool Function;
	bool saveSteplessEnabled;
} __Stepless;

void check_update_task(void *pvParameter);
esp_err_t nvs_read_string(const char* key, char* value, size_t max_len);
#endif