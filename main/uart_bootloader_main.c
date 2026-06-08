/* UART Echo Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <sys/types.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_rom_crc.h"

#include "led_bin_on.h" //-2058702718, fw:2
// #include "led_bin_off.h" //1133265113, fw:1
#include "nvs_flash.h"

#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "mqtt_client.h"


/**
 * This is an example which echos any data it receives on configured UART back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: configured UART
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 * - Pin assignment: see defines below (See Kconfig)
 */

#define ECHO_TEST_TXD 5
#define ECHO_TEST_RXD 2

#define ECHO_UART_PORT_NUM (UART_NUM_2)
#define ECHO_UART_BAUD_RATE (CONFIG_EXAMPLE_UART_BAUD_RATE)

static const char* TAG = "UART TEST";

#define BUF_SIZE (1024)

#define CHUNK_SIZE 256

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "testtopic/221m", 1);
        esp_mqtt_client_subscribe(client, "testtopic/222m", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        const int data_len = event->total_data_len;
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);

        if (event->topic_len == strlen("testtopic/221m") &&
            strncmp(event->topic, "testtopic/221m", event->topic_len) == 0)
        {
            printf("LEN=%d\n", data_len);

            uint8_t* current_dump = malloc(data_len);
            if (event->current_data_offset == 0)
            {
                if (!current_dump)
                {
                    ESP_LOGE(TAG, "malloc failed");
                    break;
                }
            }

            memcpy(
                current_dump + event->current_data_offset,
                event->data,
                event->data_len
            );

            if (event->current_data_offset + event->data_len ==
                event->total_data_len)
            {
                ESP_LOGI(TAG, "crc %d", esp_rom_crc32_le(0, current_dump, data_len));
            }
            if (current_dump)
            {
                free(current_dump);
            }
        }

        if (event->topic_len == strlen("testtopic/222m") &&
            strncmp(event->topic, "testtopic/222m", event->topic_len) == 0)
        {
            printf("DATA=%.*s\r\n", event->data_len, event->data);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = "mqtt://broker.emqx.io",
            // .verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start,
            .verification.skip_cert_common_name_check = true
        },
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void init()
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN, // UART_PARITY_DISABLE, UART_PARITY_EVEN
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, -1, -1));
    ESP_ERROR_CHECK(gpio_set_pull_mode(ECHO_TEST_RXD, GPIO_PULLDOWN_ONLY));
}

bool wait_for_ack(void)
{
    uint8_t ack;
    int len = uart_read_bytes(ECHO_UART_PORT_NUM, &ack, 1, 1000 / portTICK_PERIOD_MS);
    if (len == 1 && ack == 0x79)
    {
        // ESP_LOGI(TAG, "  ACK");
        return true;
    }
    else if (len == 1)
        ESP_LOGE(TAG, "  NACK: 0x%02X", ack);
    else
        ESP_LOGE(TAG, "  Timeout");

    return false;
}

void sync_bootloader_v2()
{
    uint8_t sync_byte = 0x7F;
    uint8_t response;

    // Step 1: Sync with bootloader
    while (1)
    {
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&sync_byte, 1);
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, &response, 1, 200 / portTICK_PERIOD_MS);
        if (len == 1 && response == 0x79)
        {
            ESP_LOGI(TAG, "Bootloader ACK received!");
            break;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void flush()
{
    uint8_t flush_buf[64];
    while (uart_read_bytes(ECHO_UART_PORT_NUM, flush_buf, sizeof(flush_buf), 10 / portTICK_PERIOD_MS) > 0)
    {
    }
}

void write_uart(uint8_t cmd[], size_t size)
{
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)cmd, size);
}

int read_uart(void* buf, uint32_t length)
{
    return uart_read_bytes(ECHO_UART_PORT_NUM, buf, length, 2000 / portTICK_PERIOD_MS);
}

void foo()
{
    // read current memory

    uint8_t full_dump[bare_led_bin_len];
    uint32_t version = -1;
    int addr_offset = CHUNK_SIZE * 2; // 512 at second chunk
    uint8_t cmd[2] = {0x11, 0xEE};
    write_uart(cmd, 2);

    if (!wait_for_ack()) return;
    unsigned int chunk_len = bare_led_bin_len - addr_offset;
    if (chunk_len > CHUNK_SIZE)
        chunk_len = CHUNK_SIZE;

    uint32_t addr = 0x08000000 + addr_offset;

    uint8_t addr_bytes[4] = {
        (addr >> 24) & 0xFF,
        (addr >> 16) & 0xFF,
        (addr >> 8) & 0xFF,
        addr & 0xFF
    };

    uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
    write_uart(addr_bytes, 4);
    write_uart(&addr_csum, 1);

    if (!wait_for_ack()) return;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t N = (uint8_t)(chunk_len - 1);
    uint8_t n_csum = (uint8_t)(~N);

    write_uart(&N, 1);
    write_uart(&n_csum, 1);

    vTaskDelay(pdMS_TO_TICKS(10));
    wait_for_ack();

    int r = read_uart(&full_dump[addr_offset], chunk_len);
    // const int r = uart_read_bytes(ECHO_UART_PORT_NUM, &full_dump[addr_offset], chunk_len, pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "r=%d", r);
    if (r != chunk_len)
    {
        ESP_LOGE(TAG, "read failed at %d\n", addr_offset);
        return;
    }
    if (addr == 0x08000200)
    {
        // special read from .firmware_meta

        version =
            (uint32_t)full_dump[addr_offset + 0] |
            (uint32_t)full_dump[addr_offset + 1] << 8 |
            (uint32_t)full_dump[addr_offset + 2] << 16 |
            (uint32_t)full_dump[addr_offset + 3] << 24;
    }


    if (version == -1)
    {
        ESP_LOGE(TAG, "No firmware version found!");
        return;
    }


    ESP_LOGI(TAG, "current firmware version = %lu", version);

    // now checks begin for new firmware
    uint32_t version_fw =
        (uint32_t)bare_led_bin[addr_offset + 0] |
        (uint32_t)bare_led_bin[addr_offset + 1] << 8 |
        (uint32_t)bare_led_bin[addr_offset + 2] << 16 |
        (uint32_t)bare_led_bin[addr_offset + 3] << 24;
    ESP_LOGI(TAG, "bare_led_bin firmware version = %lu", version_fw);

    // resets dump
    memset(full_dump, 0, sizeof(full_dump));

    for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
    {
        uint8_t cmd[2] = {0x11, 0xEE};
        write_uart(cmd, 2);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)cmd, 2);

        if (!wait_for_ack()) return;
        unsigned int chunk_len = bare_led_bin_len - addr_offset;
        if (chunk_len > CHUNK_SIZE)
            chunk_len = CHUNK_SIZE;

        uint32_t addr = 0x08000000 + addr_offset;

        uint8_t addr_bytes[4] = {
            (addr >> 24) & 0xFF,
            (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF,
            addr & 0xFF
        };

        uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
        write_uart(addr_bytes, 4);
        write_uart(&addr_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)addr_bytes, 4);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&addr_csum, 1);

        if (!wait_for_ack()) return;
        vTaskDelay(pdMS_TO_TICKS(10));

        uint8_t N = (uint8_t)(chunk_len - 1);
        uint8_t n_csum = (uint8_t)(~N);

        write_uart(&N, 1);
        write_uart(&n_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&N, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&n_csum, 1);

        vTaskDelay(pdMS_TO_TICKS(10));
        wait_for_ack();

        ESP_LOGI(TAG, "full_dump write");
        int r = read_uart(&full_dump[addr_offset], chunk_len);
        // const int r = uart_read_bytes(ECHO_UART_PORT_NUM, &full_dump[addr_offset], chunk_len, pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "r=%d", r);
        if (r != chunk_len)
        {
            ESP_LOGE(TAG, "read failed at %d\n", addr_offset);
            return;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());


    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();
    init();

    // sync_bootloader();
    sync_bootloader_v2();

    // Step 2: Flush RX
    uint8_t flush_buf[64];
    // while (uart_read_bytes(ECHO_UART_PORT_NUM, flush_buf, sizeof(flush_buf), 50 / portTICK_PERIOD_MS) > 0)
    //     ;
    /*uint8_t flush_buf[64];
    while (uart_read_bytes(ECHO_UART_PORT_NUM, flush_buf, sizeof(flush_buf), 10 / portTICK_PERIOD_MS) > 0) {}*/
    flush();

    // Step 3: Send Get command immediately (remove uart_wait_tx_done)
    uint8_t get_cmd[] = {0x00, 0xFF}; // 0x00 GET COMMAND LIST
    write_uart(get_cmd, 2);

    // Step 4: Read response
    uint8_t ack;
    int len = read_uart(&ack, 1);
    // int len = uart_read_bytes(ECHO_UART_PORT_NUM, &ack, 1, 2000 / portTICK_PERIOD_MS);
    if (len == 1 && ack == 0x79)
    {
        ESP_LOGI(TAG, "Get cmd ACK");
        // Read bootloader info...
        uint8_t num_bytes;
        len = read_uart(&num_bytes, 1);
        // len = uart_read_bytes(ECHO_UART_PORT_NUM, &num_bytes, 1, 2000 / portTICK_PERIOD_MS);
        if (len == 1)
        {
            ESP_LOGI(TAG, "Bytes to follow: %d", num_bytes);
            uint8_t data[32];
            len = read_uart(data, num_bytes);
            // len = uart_read_bytes(ECHO_UART_PORT_NUM, data, num_bytes, 2000 / portTICK_PERIOD_MS);
            if (len == num_bytes)
            {
                ESP_LOGI(TAG, "Version: 0x%02X", data[0]);
                for (int i = 1; i < num_bytes; i++)
                {
                    ESP_LOGI(TAG, "Command: 0x%02X", data[i]);
                }
                // Read final ACK
                len = read_uart(&ack, 1);
                if (len == 1 && ack == 0x79)
                {
                    ESP_LOGI(TAG, "Get command complete!");
                }
            }
        }
    }
    else
    {
        ESP_LOGI(TAG, "Get cmd NACK: 0x%02X", ack);
    }
    flush();

    // read version
    uint8_t full_dump[bare_led_bin_len];
    uint32_t version = -1;
    int addr_offset = CHUNK_SIZE * 2; // 512 at second chunk
    uint8_t cmd[2] = {0x11, 0xEE};
    write_uart(cmd, 2);

    if (!wait_for_ack()) return;
    unsigned int chunk_len = bare_led_bin_len - addr_offset;
    if (chunk_len > CHUNK_SIZE)
        chunk_len = CHUNK_SIZE;

    uint32_t addr = 0x08000000 + addr_offset;

    uint8_t addr_bytes[4] = {
        (addr >> 24) & 0xFF,
        (addr >> 16) & 0xFF,
        (addr >> 8) & 0xFF,
        addr & 0xFF
    };

    uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
    write_uart(addr_bytes, 4);
    write_uart(&addr_csum, 1);

    if (!wait_for_ack()) return;
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t N = (uint8_t)(chunk_len - 1);
    uint8_t n_csum = (uint8_t)(~N);

    write_uart(&N, 1);
    write_uart(&n_csum, 1);

    vTaskDelay(pdMS_TO_TICKS(10));
    wait_for_ack();

    int r = read_uart(&full_dump[addr_offset], chunk_len);
    // const int r = uart_read_bytes(ECHO_UART_PORT_NUM, &full_dump[addr_offset], chunk_len, pdMS_TO_TICKS(1000));

    // ESP_LOGI(TAG, "r=%d", r);
    if (r != chunk_len)
    {
        ESP_LOGE(TAG, "read failed at %d\n", addr_offset);
        return;
    }
    if (addr == 0x08000200)
    {
        // special read from .firmware_meta

        version =
            (uint32_t)full_dump[addr_offset + 0] |
            (uint32_t)full_dump[addr_offset + 1] << 8 |
            (uint32_t)full_dump[addr_offset + 2] << 16 |
            (uint32_t)full_dump[addr_offset + 3] << 24;
    }


    if (version == -1)
    {
        ESP_LOGE(TAG, "No firmware version found!");
        // possibly new flash

        // firmware flash
        for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
        {
            chunk_len = bare_led_bin_len - addr_offset;
            if (chunk_len > CHUNK_SIZE)
                chunk_len = CHUNK_SIZE;

            uint32_t addr = 0x08000000 + addr_offset;

            // Step A: Send Write Memory command
            uint8_t wm_cmd[] = {0x31, 0xCE};
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)wm_cmd, 2);
            if (!wait_for_ack())
                return; // 0x79

            // Step B: Send address (4 bytes) + checksum
            uint8_t addr_bytes[4];
            addr_bytes[0] = (addr >> 24) & 0xFF;
            addr_bytes[1] = (addr >> 16) & 0xFF;
            addr_bytes[2] = (addr >> 8) & 0xFF;
            addr_bytes[3] = addr & 0xFF;
            uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];

            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)addr_bytes, 4);
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&addr_csum, 1);
            if (!wait_for_ack())
                return; // 0x79

            // Step C: Send N + data + checksum
            uint8_t N = chunk_len - 1;
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&N, 1);
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&bare_led_bin[addr_offset], chunk_len);
            // direct from esp flash

            uint8_t data_csum = N;
            for (int i = 0; i < chunk_len; i++)
                data_csum ^= bare_led_bin[addr_offset + i];
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&data_csum, 1);
            if (!wait_for_ack())
                return; // 0x79

            ESP_LOGI(TAG, "Wrote %d bytes at 0x%08lX", chunk_len, addr);
        }
        return;
    }


    ESP_LOGI(TAG, "current firmware version = %lu", version);

    // now checks begin for new firmware
    uint32_t version_fw =
        (uint32_t)bare_led_bin[addr_offset + 0] |
        (uint32_t)bare_led_bin[addr_offset + 1] << 8 |
        (uint32_t)bare_led_bin[addr_offset + 2] << 16 |
        (uint32_t)bare_led_bin[addr_offset + 3] << 24;
    ESP_LOGI(TAG, "target firmware version = %lu", version_fw);

    // resets dump
    memset(full_dump, 0, bare_led_bin_len);

    // creates copy of current memory dump
    for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
    {
        uint8_t cmd[2] = {0x11, 0xEE};
        write_uart(cmd, 2);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)cmd, 2);

        if (!wait_for_ack()) return;
        unsigned int chunk_len = bare_led_bin_len - addr_offset;
        if (chunk_len > CHUNK_SIZE)
            chunk_len = CHUNK_SIZE;

        uint32_t addr = 0x08000000 + addr_offset;

        uint8_t addr_bytes[4] = {
            (addr >> 24) & 0xFF,
            (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF,
            addr & 0xFF
        };

        uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
        write_uart(addr_bytes, 4);
        write_uart(&addr_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)addr_bytes, 4);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&addr_csum, 1);

        if (!wait_for_ack()) return;
        vTaskDelay(pdMS_TO_TICKS(10));

        uint8_t N = (uint8_t)(chunk_len - 1);
        uint8_t n_csum = (uint8_t)(~N);

        write_uart(&N, 1);
        write_uart(&n_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&N, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&n_csum, 1);

        vTaskDelay(pdMS_TO_TICKS(10));
        wait_for_ack();

        ESP_LOGI(TAG, "full_dump write");
        int r = read_uart(&full_dump[addr_offset], chunk_len);
        // const int r = uart_read_bytes(ECHO_UART_PORT_NUM, &full_dump[addr_offset], chunk_len, pdMS_TO_TICKS(1000));

        // ESP_LOGI(TAG, "r=%d", r);
        if (r != chunk_len)
        {
            ESP_LOGE(TAG, "read failed at %d\n", addr_offset);
            return;
        }
    }

    uint32_t actual_crc = esp_rom_crc32_le(0, full_dump, bare_led_bin_len);
    uint32_t flash_fw_crc = esp_rom_crc32_le(0, bare_led_bin, bare_led_bin_len);

    ESP_LOGI(TAG, "BEFORE FLASH actual_crc %d", actual_crc);
    ESP_LOGI(TAG, "BEFORE FLASH flash_fw_crc %d", flash_fw_crc);

    // same version check
    // there can be case when memory banks are similar but not exactly same
    // meaning some areas were same hence possibly corrupt
    if (flash_fw_crc == actual_crc)
    {
        ESP_LOGI(TAG, "flash_fw_crc OK");
        ESP_LOGI(TAG, "version of firmware are same, skipping update...");
        return;
    }

    /// MASS Extended Erase command - TESTED
    while (uart_read_bytes(ECHO_UART_PORT_NUM, flush_buf, sizeof(flush_buf), 10 / portTICK_PERIOD_MS) > 0);

    uint8_t ee_cmd[] = {0x44, 0xBB}; // 0x44 ^ 0xBB = 0xFF
    write_uart(ee_cmd, 2);

    read_uart(&ack, 1);

    if (ack != 0x79)
    {
        ESP_LOGE(TAG, "EE cmd failed: 0x%02X", ack);
        return;
    }
    ESP_LOGI(TAG, "EE cmd ACK");

    // Global mass erase: N=0xFFFF, checksum=0x00
    uint8_t global[] = {0xFF, 0xFF, 0x00}; //0xFFFF = Mass erase, 0xFFFE = Bank1 erase
    write_uart(global, 3);

    // TESTED - Wait for erase complete (can take seconds)
    len = uart_read_bytes(ECHO_UART_PORT_NUM, &ack, 1, 15000 / portTICK_PERIOD_MS);
    if (len == 1 && ack == 0x79)
        ESP_LOGI(TAG, "Global erase complete!");
    else
        ESP_LOGE(TAG, "Erase timeout/NACK: 0x%02X", ack);

    flush();

    // this corrupts the FW
    bare_led_bin[CHUNK_SIZE * 3] ^= 0xFF;

    // firmware flash
    for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
    {
        chunk_len = bare_led_bin_len - addr_offset;
        if (chunk_len > CHUNK_SIZE)
            chunk_len = CHUNK_SIZE;

        uint32_t addr = 0x08000000 + addr_offset;

        // Step A: Send Write Memory command
        uint8_t wm_cmd[] = {0x31, 0xCE};
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)wm_cmd, 2);
        if (!wait_for_ack())
            return; // 0x79

        // Step B: Send address (4 bytes) + checksum
        uint8_t addr_bytes[4];
        addr_bytes[0] = (addr >> 24) & 0xFF;
        addr_bytes[1] = (addr >> 16) & 0xFF;
        addr_bytes[2] = (addr >> 8) & 0xFF;
        addr_bytes[3] = addr & 0xFF;
        uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];

        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)addr_bytes, 4);
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&addr_csum, 1);
        if (!wait_for_ack())
            return; // 0x79

        // Step C: Send N + data + checksum
        uint8_t N = chunk_len - 1;
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&N, 1);
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&bare_led_bin[addr_offset], chunk_len);
        // direct from esp flash

        uint8_t data_csum = N;
        for (int i = 0; i < chunk_len; i++)
            data_csum ^= bare_led_bin[addr_offset + i];
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&data_csum, 1);
        if (!wait_for_ack())
            return; // 0x79

        ESP_LOGI(TAG, "Wrote %d bytes at 0x%08lX", chunk_len, addr);
    }

    // after flash get CRC
    uint8_t* current_dump = malloc(bare_led_bin_len);

    for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
    {
        uint8_t cmd[2] = {0x11, 0xEE};
        write_uart(cmd, 2);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)cmd, 2);

        if (!wait_for_ack()) return;
        unsigned int chunk_len = bare_led_bin_len - addr_offset;
        if (chunk_len > CHUNK_SIZE)
            chunk_len = CHUNK_SIZE;

        uint32_t addr = 0x08000000 + addr_offset;

        uint8_t addr_bytes[4] = {
            (addr >> 24) & 0xFF,
            (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF,
            addr & 0xFF
        };

        uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
        write_uart(addr_bytes, 4);
        write_uart(&addr_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)addr_bytes, 4);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&addr_csum, 1);

        if (!wait_for_ack()) return;
        vTaskDelay(pdMS_TO_TICKS(10));

        uint8_t N = (uint8_t)(chunk_len - 1);
        uint8_t n_csum = (uint8_t)(~N);

        write_uart(&N, 1);
        write_uart(&n_csum, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&N, 1);
        // uart_write_bytes(ECHO_UART_PORT_NUM, (char*)&n_csum, 1);

        vTaskDelay(pdMS_TO_TICKS(10));
        wait_for_ack();

        ESP_LOGI(TAG, "current_dump write");
        int r = read_uart(&current_dump[addr_offset], chunk_len);
        // const int r = uart_read_bytes(ECHO_UART_PORT_NUM, &current_dump[addr_offset], chunk_len, pdMS_TO_TICKS(1000));

        // ESP_LOGI(TAG, "r=%d", r);
        if (r != chunk_len)
        {
            ESP_LOGE(TAG, "read failed at %d\n", addr_offset);
            return;
        }
    }

    actual_crc = esp_rom_crc32_le(0, current_dump, bare_led_bin_len);

    // print now current flashed FW and the given FW CRCs
    ESP_LOGI(TAG, "AFTER FLASH actual_crc %d", actual_crc);
    ESP_LOGI(TAG, "AFTER FLASH flash_fw_crc %d", flash_fw_crc);

    free(current_dump);
    current_dump = NULL;

    if (actual_crc != flash_fw_crc)
    {
        // ESP_LOGE(TAG, "flash_fw_crc failed: 0x%02X", actual_crc);
        ESP_LOGE(TAG, "current flash is corrupted");
        ESP_LOGI(TAG, "reverting flash to %lu", version);

        /// MASS Extended Erase command - TESTED
        // while (uart_read_bytes(ECHO_UART_PORT_NUM, flush_buf, sizeof(flush_buf), 10 / portTICK_PERIOD_MS) > 0);
        flush();

        uint8_t ee_cmd[] = {0x44, 0xBB}; // 0x44 ^ 0xBB = 0xFF
        write_uart(ee_cmd, 2);

        read_uart(&ack, 1);

        if (ack != 0x79)
        {
            ESP_LOGE(TAG, "EE cmd failed: 0x%02X", ack);
            return;
        }
        ESP_LOGI(TAG, "EE cmd ACK");

        // Global mass erase: N=0xFFFF, checksum=0x00
        uint8_t global[] = {0xFF, 0xFF, 0x00}; //0xFFFF = Mass erase, 0xFFFE = Bank1 erase
        write_uart(global, 3);

        // TESTED - Wait for erase complete (can take seconds)
        len = uart_read_bytes(ECHO_UART_PORT_NUM, &ack, 1, 15000 / portTICK_PERIOD_MS);
        if (len == 1 && ack == 0x79)
            ESP_LOGI(TAG, "Global erase complete!");
        else
            ESP_LOGE(TAG, "Erase timeout/NACK: 0x%02X", ack);

        flush();

        // firmware flash
        bare_led_bin_len = bare_led_bin_len;
        for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
        // for (addr_offset = 0; addr_offset < bare_led_bin_len; addr_offset += CHUNK_SIZE)
        {
            chunk_len = bare_led_bin_len - addr_offset;
            if (chunk_len > CHUNK_SIZE)
                chunk_len = CHUNK_SIZE;

            uint32_t addr = 0x08000000 + addr_offset;

            // Step A: Send Write Memory command
            uint8_t wm_cmd[] = {0x31, 0xCE};
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)wm_cmd, 2);
            if (!wait_for_ack())
                return; // 0x79

            // Step B: Send address (4 bytes) + checksum
            uint8_t addr_bytes[4];
            addr_bytes[0] = (addr >> 24) & 0xFF;
            addr_bytes[1] = (addr >> 16) & 0xFF;
            addr_bytes[2] = (addr >> 8) & 0xFF;
            addr_bytes[3] = addr & 0xFF;
            uint8_t addr_csum = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];

            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)addr_bytes, 4);
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&addr_csum, 1);
            if (!wait_for_ack())
                return; // 0x79

            // Step C: Send N + data + checksum
            uint8_t N = chunk_len - 1;
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&N, 1);
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&full_dump[addr_offset], chunk_len);
            // direct from esp flash

            uint8_t data_csum = N;
            for (int i = 0; i < chunk_len; i++)
                data_csum ^= full_dump[addr_offset + i];
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)&data_csum, 1);
            if (!wait_for_ack())
                return; // 0x79

            ESP_LOGI(TAG, "Wrote %d bytes at 0x%08lX", chunk_len, addr);
        }
        actual_crc = esp_rom_crc32_le(0, full_dump, bare_led_bin_len);
        ESP_LOGI(TAG, "AFTER FLASH ROLLBACK actual_crc %d", actual_crc);
    }

    // Step D: Go command — jump to firmware
    uint8_t go_cmd[] = {0x21, 0xDE};
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)go_cmd, 2);
    if (!wait_for_ack())
        return;

    uint8_t go_addr[] = {0x08, 0x00, 0x00, 0x00, 0x08}; // 0x08000000 + checksum
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char*)go_addr, 5);
    if (!wait_for_ack())
        return;

    ESP_LOGI(TAG, "Flashing complete.");

    // Infinite loop
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
