/*
Copyright (c) 2025 Tulio Carvalho
Licensed under the MIT License. See LICENSE file for details.
*/


#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE 
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "freertos/semphr.h"
#include "esp_err.h"

#include "soc/soc_caps.h"
#include "esp_system.h"  //?
#include "driver/uart.h" 
#include "string.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"


/*************************************************/
/************* Tags *************/
/*************************************************/
static const char* TAGGpio = "GPIO";
static const char *TAGUART = "RX_TASK";
static const char *TAGMQTT = "MQTT_EXAMPLE";

/*************************************************/
/************** DEFINES **************************/
/*************************************************/

#define GPIO_INPUT_IO_0     0//CONFIG_GPIO_INPUT_0
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0)) 

#define GPIO_OUTPUT_IO_0    2//CONFIG_GPIO_OUTPUT_1
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<GPIO_OUTPUT_IO_0) 

#define ESP_INTR_FLAG_DEFAULT 0 // Definição de tratamento de Flag default = 0 ;

#define TXD_PIN (GPIO_NUM_5) 
#define RXD_PIN (GPIO_NUM_4)

#define qos_ex 0
static char *Client_ID; 
static char *uri_default;  


/**********************************************/
/************* types **************************/
/**********************************************/
typedef struct {
    uint64_t event_count;
    uint64_t event_alarm;
}  timer_values_element_t ;
typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
}  rtc_element_t ;

typedef struct {
    bool automatic;
    uint16_t basic_duty;
} pwm_element_t ;

typedef struct {
    uint8_t num;
    uint16_t basic_duty;
} pwmqtt_element_t ;


typedef struct {
   int adc_raw;
   int voltage;
} adc_element_t ;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

static const int RX_BUF_SIZE = 1024;

/**********************************************/
/*************** Queue ************************/
/**********************************************/

static QueueHandle_t efm_queue = NULL;
static QueueHandle_t efm_atualiza = NULL;
static SemaphoreHandle_t gpio_semaphoro = NULL;

bool strcmpr_tc(char *str1, char *str2, int str_len)
{
    uint8_t i = 0;
    uint8_t c = 0;
    bool result = false;
    while (i < str_len)
    {
        if ((*str1) == (*str2))
        {
            c++;
        }

        str1++;
        str2++;
        i++;
    }

    if (c == str_len)
        result = true;

    return result;
}

/***********************************************/
/************* INITs ***************************/
/*************************************************/

void init_uart(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200, //9600
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
        // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAGMQTT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_app_start(char *uri)
{

    esp_mqtt_client_config_t mqtt_cfg = {
      //  .broker.address.uri = "mqtt://com2efm:com2efm@node02.myqtthub.com:1883",
        .credentials.client_id = Client_ID,
        //.broker.address.uri = "mqtt://0.tcp.sa.ngrok.io:12941",
        .broker.address.uri =  uri,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

/****************************************************/
/************* Interruptions *************************/
/****************************************************/
esp_mqtt_client_handle_t client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAGMQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;

    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "efm2com", qos_ex);
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "efm_number", qos_ex);
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);


        msg_id = esp_mqtt_client_subscribe(client, "red_button", qos_ex);  
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_DISCONNECTED");
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        ESP_LOG_BUFFER_HEXDUMP(TAGMQTT,  event->data, event->data_len, ESP_LOG_INFO);

         if (strcmpr_tc ( event->topic, "efm_number", strlen("efm_number") ) ) {
            
            uint32_t efm_escolhido = 0;
            char *x = malloc(5);
            snprintf(x, 5, "%.*s", event->data_len, event->data);
            efm_escolhido = atoi(x);

            xQueueSendFromISR(efm_queue,&efm_escolhido, NULL); 
            
            free(x);

            }  if (strcmpr_tc ( event->topic, "red_button", strlen("red_button") ) ) {
            
            nvs_handle_t my_handle_nvs;
            char *uri_from_nvs = malloc(100);
            sprintf(uri_from_nvs, "%.*s", event->data_len, event->data);

            nvs_open("MQTT", NVS_READWRITE, &my_handle_nvs);
            
            nvs_set_str(my_handle_nvs, "uri", uri_from_nvs);
            free(uri_from_nvs);

            nvs_close(my_handle_nvs);
 
            printf("Restarting now.\n");
            fflush(stdout);
            esp_restart();

                
            
            }  else{

                uart_write_bytes(UART_NUM_1, event->data, event->data_len);
            }


        
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAGMQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAGMQTT, "Other event id:%d", event->event_id);
        break;
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    xSemaphoreGiveFromISR( gpio_semaphoro, NULL );
}
 
/****************************************************/
/******************** TASKS *************************/
/****************************************************/

static void gpio_task_botao(void* arg)
{
    for(;;) {
        
     if( xSemaphoreTake( gpio_semaphoro, portMAX_DELAY ) )   
     {

            nvs_handle_t gpio_handle_nvs;


            char* gpio_nvs = malloc(100);

                sprintf(gpio_nvs, uri_default);
            nvs_open("MQTT", NVS_READWRITE, &gpio_handle_nvs);
            nvs_set_str(gpio_handle_nvs, "uri", gpio_nvs);
            free(gpio_nvs);

            nvs_close(gpio_handle_nvs);
    
            printf("TIME OUT - Restarting now.\n");
            fflush(stdout);
            esp_restart();
        }

    }
}

static void uart_task(void *arg)
{
    init_uart();
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    char *efm_topic = malloc(20);
    snprintf(efm_topic, 8, "com2efm"); 
    uint32_t num_atual = 0;
    while (1) {
        if (xQueueReceive(efm_atualiza, &num_atual, 0)) {
       
            if (num_atual == 0){
                snprintf(efm_topic, 8, "com2efm");      
            } else{
                snprintf(efm_topic, 18, "com2efm%"PRIu32"", num_atual);  
            }
        printf(efm_topic); 
        printf("\n"); 
        }
        
        
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 10 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TAGUART, "Read %d bytes: '%s'", rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(TAGUART, data, rxBytes, ESP_LOG_INFO);
           esp_mqtt_client_publish(client, efm_topic, (char *)data, rxBytes, qos_ex, 0); // Normal Broadcast
        }
       
    }
    free(data);
}

/****************************************************/
/**************** Main ******************************/
/*****************************************************/

static void main_task(void *arg)
{
    /**************************************************/
    /******************** I/O *************************/
    /**************************************************/
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL; // seleção do pino 2. 
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // Configura Entrada

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, NULL);
    gpio_set_level(GPIO_OUTPUT_IO_0, 0);



    /*****************************************************
     * MQttt 
     *  
    ****************************************************/ 

    ESP_LOGI(TAGMQTT, "[APP] Startup..");
    ESP_LOGI(TAGMQTT, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAGMQTT, "[APP] IDF version: %s", esp_get_idf_version());

    //esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*  This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    nvs_handle_t my_handle_nvs;
    
    char* uri_from_nvs = malloc(100);
      
    if (nvs_open("MQTT", NVS_READONLY, &my_handle_nvs) == ESP_OK){
        size_t required_size;
        nvs_get_str(my_handle_nvs, "uri", NULL, &required_size);
        nvs_get_str(my_handle_nvs, "uri", uri_from_nvs, &required_size);
        
        ESP_LOGI(TAGMQTT, "Lido da memoria Uri: %s", uri_from_nvs);
    } 
    else{
        
        nvs_open("MQTT", NVS_READWRITE, &my_handle_nvs);
        
        sprintf(uri_from_nvs, uri_default);
        
        nvs_set_str(my_handle_nvs, "uri", uri_from_nvs);
        ESP_LOGI(TAGMQTT, "Gravado novo Uri: %s", uri_from_nvs);
        
    }
     
    nvs_close(my_handle_nvs);

    mqtt_app_start(uri_from_nvs);

    efm_queue =  xQueueCreate(1, sizeof(uint32_t));
    efm_atualiza = xQueueCreate(1, sizeof(uint32_t));
    gpio_semaphoro = xSemaphoreCreateBinary(); 
      
    /*****************************************************/
    /*************** tasks *******************************/
    /*****************************************************/
   
    xTaskCreate(gpio_task_botao, "gpio_task_botao", 2048, NULL, 5, NULL);
    
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 20, NULL);

    /******************************************************/
    /*************LOG **************/
    /******************************************************/
    esp_log_level_set(TAGGpio, ESP_LOG_ERROR);
    esp_log_level_set(TAGUART, ESP_LOG_INFO);


    uint32_t efm_status = 0;
    while(1)
    {

        if ( xQueueReceive(efm_queue, &efm_status, portMAX_DELAY ) ) {
            
            xQueueSendToBack(efm_atualiza, &efm_status, 10/ portTICK_PERIOD_MS);
            if (efm_status == 0){
                gpio_set_level(GPIO_OUTPUT_IO_0, 0); 
                vTaskDelay(3000 / portTICK_PERIOD_MS); 
                gpio_set_level(GPIO_OUTPUT_IO_0, 1);

            } else {
                for(int iii = 0; iii <= efm_status; iii++){ 
                     gpio_set_level(GPIO_OUTPUT_IO_0, 0); 
                     vTaskDelay(500 / portTICK_PERIOD_MS); 
                     gpio_set_level(GPIO_OUTPUT_IO_0, 1);
                     vTaskDelay(500 / portTICK_PERIOD_MS);
                }
                
            }

        }
       
    }
   
}

void init_espLoggernet(char *uri, char *clientid){

    if (Client_ID != NULL) {
        free(Client_ID);
    }
    if (uri_default != NULL) {
        free(uri_default);
    }
    
    uri_default = malloc(strlen(uri) + 1);
    Client_ID = malloc(strlen(clientid) + 1);
    
    if (uri_default != NULL) {
        strcpy(uri_default, uri);  
    } else {
        printf("Error.\n");
    }
    if (Client_ID != NULL) {
        strcpy(Client_ID, clientid);  
    } else {
        printf("Error.\n");
    }

    xTaskCreate(main_task, "main_task", 4096, NULL, 2, NULL);
}
