idf_component_register(
    SRCS "app_main.c" "app_state.c" "board_ui.c" "synth_service.c" "weather_service.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp-gsp esp_wifi esp_netif nvs_flash espressif__esp-sr espressif__esp32_s31_korvo_1_noglib espressif__esp_audio_effects espressif__esp_codec_dev bt esp_ringbuf esp_http_client espressif__cjson mbedtls)

# esp-dl 3.3.11 uses C-style typedefs for C++ structs with member
# initializers. GCC 16 diagnoses this as -Wnon-c-typedef-for-linkage; ESP-IDF
# promotes warnings to errors. Keep the compatibility exception local to the
# third-party target, without modifying its managed source.
idf_component_get_property(esp_dl_target espressif__esp-dl COMPONENT_LIB)
target_compile_options(${esp_dl_target} PRIVATE
    -Wno-error=non-c-typedef-for-linkage)

gsp_add_bundle(${COMPONENT_LIB}
    SCENES ../scenes/korvo_home_800.json
           ../scenes/korvo_synth_800.json
           ../scenes/korvo_weather_800.json
           ../scenes/korvo_voice_800.json
           ../scenes/korvo_object_800.json
           ../scenes/korvo_lighting_800.json
           ../scenes/korvo_clock_timer_800.json
           ../scenes/korvo_calculator_800.json
           ../scenes/korvo_food_800.json
           ../scenes/korvo_wifi_800.json
           ../scenes/korvo_bluetooth_800.json
    PIXEL_FORMAT rgb565)
