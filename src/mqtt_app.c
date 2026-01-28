#include "mqtt_app.h"

#include <string.h>

#include "main.h"

static const char *BROKER_URL = "mqtts://a3qi68kx39kjfk-ats.iot.eu-central-1.amazonaws.com:8883";

esp_mqtt_client_handle_t client;
bool is_mqtt_ready = false;

static uint8_t s_subscribe_counter = 0;

extern S_EEPROM gRDEeprom;
extern uint16_t Read_Eeprom_Request_Index;
extern bool Quarke_Partition_State;
extern bool Quarke_Update_task_Flag;
extern bool Bootloader_State_Flag;
extern TaskHandle_t Quarke_Update_Task_xHandle;
extern bool Wifi_Connected_Flag;
extern void Quarke_Update_task(void *pvParameters);

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(GATTS_TABLE_TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_build_topic(char *buffer, size_t buffer_size, const char *address, const char *suffix) {
    if (address == NULL || suffix == NULL) {
        if (buffer_size > 0) {
            buffer[0] = '\0';
        }
        return;
    }
    snprintf(buffer, buffer_size, "/%s/%s", address, suffix);
}

void mqtt_subscribe_app_topics(const char *address) {
    char topic_buffer[64];
    int msg_id;

    if (address == NULL || strlen(address) == 0) {
        return;
    }

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, "app/eeprom");
    msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
    ESP_LOGI(GATTS_TABLE_TAG, "sent subscribe successful, msg_id=%d", msg_id);

    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, "app/request");
    msg_id = esp_mqtt_client_subscribe(client, topic_buffer, 0);
    ESP_LOGI(GATTS_TABLE_TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

void publish_debug_message(const uint8_t *data, size_t data_len, const char *topic, const char *address) {
    char json_buffer[1024];
    int json_len;

    json_len = snprintf(json_buffer, sizeof(json_buffer), "{\"message\":\"");

    for (size_t i = 0; i < data_len; i++) {
        json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "%d", data[i]);
        if (i < data_len - 1) {
            json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, ",");
        }
    }

    json_len += snprintf(json_buffer + json_len, sizeof(json_buffer) - json_len, "\",\"topic\":\"%s\",\"address\":\"%s\"}", topic, address);

    if (is_mqtt_ready) {
        int msg_id = esp_mqtt_client_publish(client, topic, json_buffer, json_len, 0, 0);
        ESP_LOGI(GATTS_TABLE_TAG, "%s publish successful, msg_id=%d", topic, msg_id);
    }
}

void mqtt_publish_with_suffix(const char *address, const char *suffix, const uint8_t *data, size_t data_len) {
    char topic_buffer[64];
    mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, suffix);
    publish_debug_message(data, data_len, topic_buffer, address);
}

void mqtt_publish_eeprom(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/eeprom", data, data_len);
}

void mqtt_publish_polling(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/polling", data, data_len);
}

void mqtt_publish_debug(const char *address, const uint8_t *data, size_t data_len) {
    mqtt_publish_with_suffix(address, "esp/debug", data, data_len);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(GATTS_TABLE_TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client                        = event->client;
    const char *address           = (const char *)&gRDEeprom.SerialString;
    char topic_buffer[64];

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_CONNECTED");
            mqtt_subscribe_app_topics(address);
            if ((Quarke_Partition_State) && (!Quarke_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2 * 8192, NULL, 5, &Quarke_Update_Task_xHandle);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_DISCONNECTED");
            is_mqtt_ready = false;
            s_subscribe_counter = 0;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            if (s_subscribe_counter == 1) {
                is_mqtt_ready = true;
                s_subscribe_counter = 0;
                ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_SUBSCRIBTION FINISHED.");
            } else {
                s_subscribe_counter++;
            }
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, "app/eeprom");
            printf("compare_eeprom=%d\r\n", strncmp(event->topic, topic_buffer, event->topic_len));

            if (strncmp(event->topic, topic_buffer, event->topic_len) == 0) {
                memcpy(&gRDEeprom, event->data, sizeof(gRDEeprom));
                ESP_LOGI(GATTS_TABLE_TAG, "Speed : %d", gRDEeprom.sel_idxStepMotors);
                Read_Eeprom_Request_Index |= 0x800;
                Read_Eeprom_Request_Index |= 0x1000;
                Read_Eeprom_Request_Index |= 0x2000;
                Read_Eeprom_Request_Index |= 0x4000;
                Read_Eeprom_Request_Index |= 0x8000;
                ESP_LOGI(GATTS_TABLE_TAG, "Invio scrittura completata");
            }

            mqtt_build_topic(topic_buffer, sizeof(topic_buffer), address, "app/request");
            printf("compare_request=%d\r\n", strncmp(event->topic, topic_buffer, event->topic_len));
            if (strncmp(event->topic, topic_buffer, event->topic_len) == 0) {
                printf("READ DATA EEPROM\r\n");
                if (is_mqtt_ready) {
                    mqtt_publish_eeprom(address, (u_int8_t *)&gRDEeprom, sizeof(gRDEeprom));
                    ESP_LOGI(GATTS_TABLE_TAG, "eeprom sent publish successful");
                }
            }

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(GATTS_TABLE_TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(GATTS_TABLE_TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            if ((Quarke_Partition_State) && (!Quarke_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Quarke_Update_task, "Quarke_Update_task", 2 * 8192, NULL, 5, &Quarke_Update_Task_xHandle);
            }
            break;
        default:
            ESP_LOGI(GATTS_TABLE_TAG, "Other event id:%d", event->event_id);
            break;
    }
}

static void mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                     = BROKER_URL,
        .broker.verification.certificate        = (const char *)aws_root_ca_pem_start,
        .credentials.authentication.certificate = (const char *)certificate_pem_crt_start,
        .credentials.authentication.key         = (const char *)private_pem_key_start,
    };

    ESP_LOGI(GATTS_TABLE_TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void mqtt_task(void *arg) {
    vTaskDelay(1000);

    if (!Wifi_Connected_Flag) {
        ESP_ERROR_CHECK(wifi_connect(WIFI_SSID, WIFI_PASSWORD));
    }

    mqtt_app_start();

    while (1) {
        if (wifi_is_ssid_send == true && wifi_is_pass_send == true && connect_to_wifi == true) {
            ESP_LOGI(GATTS_TABLE_TAG, "WIFI_SSID: %s", WIFI_SSID);
            ESP_LOGI(GATTS_TABLE_TAG, "WIFI_PASSWORD: %s", WIFI_PASSWORD);

            wifi_disconnect();
            ESP_ERROR_CHECK(wifi_connect(WIFI_SSID, WIFI_PASSWORD));

            wifi_is_ssid_send = false;
            wifi_is_pass_send = false;
            connect_to_wifi   = false;
        }

        vTaskDelay(1);
    }
}
