idf_component_register(
    SRCS "app_main.c" "app_state.c" "board_ui.c" "synth_service.c" "weather_service.c"
         "ui/ui_theme.c" "ui/ui_weather.c" "ui/ui_synth.c" "ui/ui_home.c" "ui/ui_drawer.c" "ui/ui_wifi.c" "ui/ui_bluetooth.c" "ui/ui_apps.c" "ui/ui.c"
         "ui/ui_img_weather_day.c" "ui/ui_img_weather_night.c"
         "ui/ui_font_cjk_14.c" "ui/ui_font_cjk_16.c" "ui/ui_font_cjk_20.c" "ui/ui_font_cjk_32.c"
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES esp_wifi esp_netif nvs_flash espressif__esp-sr espressif__esp32_s31_korvo_1_noglib espressif__esp_audio_effects espressif__esp_codec_dev bt esp_ringbuf esp_http_client espressif__cjson mbedtls lvgl espressif__esp_lvgl_adapter espressif__esp_lv_lottie_player espressif__thorvg espressif__esp_new_jpeg)

# esp-dl 3.3.11 uses C-style typedefs for C++ structs with member
# initializers. GCC 16 diagnoses this as -Wnon-c-typedef-for-linkage; ESP-IDF
# promotes warnings to errors. Keep the compatibility exception local to the
# third-party target, without modifying its managed source.
idf_component_get_property(esp_dl_target espressif__esp-dl COMPONENT_LIB)
if(esp_dl_target)
    target_compile_options(${esp_dl_target} PRIVATE
        -Wno-error=non-c-typedef-for-linkage)
endif()
