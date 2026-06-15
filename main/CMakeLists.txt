idf_component_register(
    SRCS 
        "main.c"
        "midi_uart.c"
        "midi_class_driver_txrx.c"
        "globals.c"
        "oled_display.c"
        "navigation.c"
        "power_management.c"
        "usb_daemon.c"        
        "midi_storage.c"
        "led_rgb.c"

    INCLUDE_DIRS 
        "."

    REQUIRES 
        freertos
        driver
        esp_timer
        ssd1306
        nvs_flash
        esp_pm
        led_strip

    PRIV_REQUIRES 
        usb
)