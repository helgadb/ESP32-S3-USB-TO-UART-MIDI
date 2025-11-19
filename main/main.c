#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "usb/usb_host.h"

#include "midi_class_driver_txrx.h"
#include "midi_uart.h"

static const char *TAG = "MIDI_MAIN";

//==========================
// CONFIGURAÇÕES
//==========================
#define DAEMON_TASK_PRIORITY    2
#define CLASS_TASK_PRIORITY     3

#define UART_TASK_PRIORITY      (CLASS_TASK_PRIORITY + 1)
#define USB2UART_TASK_PRIORITY  (CLASS_TASK_PRIORITY + 4)

#define UART_STACK_SIZE         4096
#define USB_STACK_SIZE          4096
#define DAEMON_STACK_SIZE       4096
#define CLASS_STACK_SIZE        8192

#define UART_NUM_CFG            UART_NUM_1


//==========================
// TASK: USB Host Daemon
//==========================
static void usb_daemon_task(void *arg)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(TAG, "Installing USB Host Library...");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    xSemaphoreGive(sem);
    vTaskDelay(20);

    while (1) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            ESP_LOGW(TAG, "USB: no clients");
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
            ESP_LOGW(TAG, "USB: no devices connected");
    }
}


//==========================
// TASK: UART → USB
//==========================
static void uart_to_usb_task(void *arg)
{
    uint8_t data[1024];

    ESP_LOGI(TAG, "UART→USB task started");

    while (1)
    {
        int len = uart_read_bytes(
            UART_NUM_CFG,
            data,
            sizeof(data),
            pdMS_TO_TICKS(10)     // suficiente para yield
        );

        if (len > 0) {
            midi_uart_parse_and_send_to_usb(data, len);
        }

        // --- IMPORTANTE ---
        // Evita travar o watchdog mesmo sob carga alta
        vTaskDelay(1);
    }
}


//==========================
// Hook (USB → UART fallback)
//==========================
void process_usb_rx_for_uart(const uint8_t *data, size_t length)
{
    midi_uart_send_to_uart(data, length);
}


//==========================
// APP MAIN
//==========================
void app_main(void)
{
    SemaphoreHandle_t ready_sem = xSemaphoreCreateBinary();

    TaskHandle_t usb_daemon_hdl;
    TaskHandle_t class_driver_hdl;
    TaskHandle_t uart_to_usb_hdl;

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "     Starting MIDI Translator     ");
    ESP_LOGI(TAG, "=================================");

    // 1) Inicializar UART MIDI
    midi_uart_init();

    // 2) Iniciar task de baixa latência USB→UART
    midi_uart_start_usb_to_uart_task(
        USB2UART_TASK_PRIORITY,   // alta prioridade
        USB_STACK_SIZE,
        1                         // CPU 1
    );

    // 3) Task USB Host Daemon
    xTaskCreatePinnedToCore(
        usb_daemon_task,
        "usb_daemon",
        DAEMON_STACK_SIZE,
        ready_sem,
        DAEMON_TASK_PRIORITY,
        &usb_daemon_hdl,
        0
    );

    // 4) Task principal do driver USB MIDI
    xTaskCreatePinnedToCore(
        class_driver_task,
        "usb_midi_class",
        CLASS_STACK_SIZE,
        ready_sem,
        CLASS_TASK_PRIORITY,
        &class_driver_hdl,
        0
    );

    // 5) Task UART → USB (corrigida)
    xTaskCreatePinnedToCore(
        uart_to_usb_task,
        "uart_to_usb",
        UART_STACK_SIZE,
        NULL,
        UART_TASK_PRIORITY,
        &uart_to_usb_hdl,
        1
    );

    ESP_LOGI(TAG, "System Ready. MIDI pass-through active.");
}
