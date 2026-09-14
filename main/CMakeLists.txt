idf_component_register(SRCS "app_main.c" "audio_io.c" "dsp_goertzel.c" "dominoex.c" "dominovar.c" "tx_nco.c" "ptt.c" "ble_server.c" "cat.c" "rx_resample.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash
                    PRIV_REQUIRES esp_driver_dac esp_driver_gpio esp_adc esp_driver_uart)
