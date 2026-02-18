#include "mqtt_app.h"

#include <string.h>

#include "main.h"

static const char *BROKER_URL = "mqtts://a3qi68kx39kjfk-ats.iot.eu-central-1.amazonaws.com:8883";
static const char *TAG_WIFI_MANAGER = "wifi_manager";

#define WIFI_REQ_QUEUE_LEN 8
#define WIFI_MAX_RETRY 3
#define WIFI_RETRY_BASE_MS 500
#define WIFI_APPLY_COOLDOWN_MS 800

typedef enum {
    WIFI_MANAGER_IDLE = 0,
    WIFI_MANAGER_APPLYING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_FAILED,
} wifi_manager_state_t;

typedef struct {
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
    bool persist_to_nvs;
    uint32_t request_id;
    TickType_t enqueue_tick;
    char source[16];
} wifi_request_t;

esp_mqtt_client_handle_t client;
bool is_mqtt_ready = false;

static uint8_t s_subscribe_counter = 0;
static QueueHandle_t s_wifi_req_queue = NULL;
static wifi_manager_state_t s_wifi_manager_state = WIFI_MANAGER_IDLE;
static uint32_t s_wifi_request_counter = 0;
static TickType_t s_last_apply_tick = 0;

extern S_EEPROM gRDEeprom;
extern uint16_t Read_Eeprom_Request_Index;
extern bool Unit_Partition_State;
extern bool Unit_Update_task_Flag;
extern bool Bootloader_State_Flag;
extern TaskHandle_t Unit_Update_Task_xHandle;
extern bool Wifi_Connected_Flag;
extern void Unit_Update_task(void *pvParameters);

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(GATTS_TABLE_TAG, "Last error %s: 0x%x", message, error_code);
    }
}

bool mqtt_enqueue_wifi_credentials(const char *ssid, const char *password, bool persist_to_nvs, const char *source_tag) {
    if (s_wifi_req_queue == NULL) {
        s_wifi_req_queue = xQueueCreate(WIFI_REQ_QUEUE_LEN, sizeof(wifi_request_t));
    }

    if (s_wifi_req_queue == NULL || ssid == NULL || password == NULL) {
        return false;
    }

    if (ssid[0] == '\0') {
        ESP_LOGW(TAG_WIFI_MANAGER, "Rejected Wi-Fi request: empty SSID");
        return false;
    }

    wifi_request_t request;
    memset(&request, 0, sizeof(request));
    strlcpy(request.ssid, ssid, sizeof(request.ssid));
    strlcpy(request.password, password, sizeof(request.password));
    request.persist_to_nvs = persist_to_nvs;
    request.enqueue_tick = xTaskGetTickCount();
    request.request_id = ++s_wifi_request_counter;
    strlcpy(request.source, source_tag ? source_tag : "unknown", sizeof(request.source));

    if (xQueueSend(s_wifi_req_queue, &request, 0) != pdTRUE) {
        // Queue full: drop oldest and keep the latest request.
        wifi_request_t discarded;
        (void)xQueueReceive(s_wifi_req_queue, &discarded, 0);
        if (xQueueSend(s_wifi_req_queue, &request, 0) != pdTRUE) {
            ESP_LOGW(TAG_WIFI_MANAGER, "Unable to enqueue Wi-Fi request id=%lu from %s", (unsigned long)request.request_id, request.source);
            return false;
        }
    }

    ESP_LOGI(TAG_WIFI_MANAGER, "Queued Wi-Fi request id=%lu source=%s ssidLen=%u passLen=%u",
             (unsigned long)request.request_id, request.source, (unsigned)strlen(request.ssid), (unsigned)strlen(request.password));
    return true;
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
            if ((Unit_Partition_State) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2 * 8192, NULL, 5, &Unit_Update_Task_xHandle);
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
            if ((Unit_Partition_State) && (!Unit_Update_task_Flag) && (!Bootloader_State_Flag)) {
                xTaskCreate(&Unit_Update_task, "Unit_Update_task", 2 * 8192, NULL, 5, &Unit_Update_Task_xHandle);
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

static esp_err_t apply_wifi_request(const wifi_request_t *request) {
    esp_err_t err;
    int retry = 0;
    TickType_t now = xTaskGetTickCount();

    if (Wifi_Connected_Flag &&
        strcmp(WIFI_SSID, request->ssid) == 0 &&
        strcmp(WIFI_PASSWORD, request->password) == 0) {
        s_wifi_manager_state = WIFI_MANAGER_CONNECTED;
        ESP_LOGI(TAG_WIFI_MANAGER, "Skip Wi-Fi request id=%lu: credentials unchanged", (unsigned long)request->request_id);
        return ESP_OK;
    }

    if ((now - s_last_apply_tick) < pdMS_TO_TICKS(WIFI_APPLY_COOLDOWN_MS)) {
        TickType_t wait_ticks = pdMS_TO_TICKS(WIFI_APPLY_COOLDOWN_MS) - (now - s_last_apply_tick);
        vTaskDelay(wait_ticks);
    }

    if (request->persist_to_nvs) {
        err = nvs_write_string("wifi_ssid", request->ssid);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIFI_MANAGER, "NVS write SSID failed id=%lu err=%s", (unsigned long)request->request_id, esp_err_to_name(err));
            return err;
        }
        err = nvs_write_string("wifi_pass", request->password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIFI_MANAGER, "NVS write PASSWORD failed id=%lu err=%s", (unsigned long)request->request_id, esp_err_to_name(err));
            return err;
        }
    }

    strlcpy(WIFI_SSID, request->ssid, sizeof(WIFI_SSID));
    strlcpy(WIFI_PASSWORD, request->password, sizeof(WIFI_PASSWORD));

    s_wifi_manager_state = WIFI_MANAGER_APPLYING;
    (void)wifi_disconnect();

    while (retry < WIFI_MAX_RETRY) {
        TickType_t attempt_start = xTaskGetTickCount();
        err = wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        TickType_t elapsed_ms = (xTaskGetTickCount() - attempt_start) * portTICK_PERIOD_MS;

        if (err == ESP_OK) {
            s_wifi_manager_state = WIFI_MANAGER_CONNECTED;
            s_last_apply_tick = xTaskGetTickCount();
            ESP_LOGI(TAG_WIFI_MANAGER, "Wi-Fi request id=%lu applied in %lu ms (retry=%d)",
                     (unsigned long)request->request_id, (unsigned long)elapsed_ms, retry);
            return ESP_OK;
        }

        retry++;
        s_wifi_manager_state = WIFI_MANAGER_FAILED;
        ESP_LOGW(TAG_WIFI_MANAGER, "Wi-Fi request id=%lu failed retry=%d err=%s elapsed=%lu ms",
                 (unsigned long)request->request_id, retry, esp_err_to_name(err), (unsigned long)elapsed_ms);

        if (retry < WIFI_MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_BASE_MS * retry));
        }
    }

    return err;
}

void mqtt_task(void *arg) {
    wifi_request_t request;
    wifi_request_t latest;

    if (s_wifi_req_queue == NULL) {
        s_wifi_req_queue = xQueueCreate(WIFI_REQ_QUEUE_LEN, sizeof(wifi_request_t));
    }
    if (s_wifi_req_queue == NULL) {
        ESP_LOGE(TAG_WIFI_MANAGER, "Failed to create Wi-Fi request queue");
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    (void)mqtt_enqueue_wifi_credentials(WIFI_SSID, WIFI_PASSWORD, false, "boot");

    while (1) {
        if (xQueueReceive(s_wifi_req_queue, &request, pdMS_TO_TICKS(200)) == pdTRUE) {
            latest = request;
            while (xQueueReceive(s_wifi_req_queue, &request, 0) == pdTRUE) {
                latest = request;
            }

            TickType_t queue_delay_ms = (xTaskGetTickCount() - latest.enqueue_tick) * portTICK_PERIOD_MS;
            ESP_LOGI(TAG_WIFI_MANAGER, "Applying Wi-Fi request id=%lu source=%s queueDelay=%lu ms",
                     (unsigned long)latest.request_id, latest.source, (unsigned long)queue_delay_ms);

            if (apply_wifi_request(&latest) == ESP_OK && !is_mqtt_ready) {
                mqtt_app_start();
            }
        }
    }
}
