if(IDF_TARGET STREQUAL "esp32p4")
    set(board_component espressif__esp32_p4_function_ev_board_noglib)
    set(board_linker_fragments)
else()
    set(board_component espressif__esp32_s31_korvo_1_noglib)
    set(board_linker_fragments linker.lf)
endif()

idf_component_register(
    SRCS "app_main.c" "app_state.c" "app_health.c" "storage_service.c" "board_ui.c" "synth_service.c"
         "vision_camera.c" "vision_service.cpp" "voice_service.c" "weather_service.c"
         "calculator_engine.c" "clock_service.c" "fireworks_engine.c" "food_service.c"
         "ui/ui_theme.c" "ui/ui_weather.c" "ui/ui_synth.c" "ui/ui_home.c" "ui/ui_drawer.c" "ui/ui_wifi.c" "ui/ui_bluetooth.c"
         "ui/ui_apps.c" "ui/ui_food.c" "ui/ui_calculator.c" "ui/ui_clock.c" "ui/ui_fireworks.c" "ui/ui_yokai_art.c" "ui/ui.c"
         "ui/ui_wifi_signal.c" "ui/ui_img_key_icon.c" "ui/ui_image_loader.c"
         "ui/ui_img_home_p1.c" "ui/ui_img_home_p2.c"
         "ui/ui_img_weather_sunny.c" "ui/ui_img_weather_cloudy.c" "ui/ui_img_weather_rain.c" "ui/ui_img_weather_night.c"
         "ui/ui_img_calculator_bg.c" "ui/ui_img_clock_bg.c" "ui/ui_img_fireworks_bg.c"
         "ui/ui_font_cjk_14.c" "ui/ui_font_cjk_16.c" "ui/ui_font_cjk_20.c" "ui/ui_font_cjk_32.c"
    INCLUDE_DIRS "." "ui"
    LDFRAGMENTS ${board_linker_fragments}
    PRIV_REQUIRES esp_wifi esp_netif nvs_flash spiffs espressif__esp-sr espressif__esp_asrc espressif__esp-dl espressif__human_face_detect espressif__esp-dsp ${board_component} espressif__esp_audio_effects espressif__esp_codec_dev bt esp_ringbuf esp_http_client espressif__cjson mbedtls lvgl espressif__esp_lvgl_adapter espressif__esp_lv_lottie_player espressif__thorvg espressif__esp_new_jpeg)

# esp-dl 3.3.11 uses C-style typedefs for C++ structs with member
# initializers. GCC 16 diagnoses this as -Wnon-c-typedef-for-linkage; ESP-IDF
# promotes warnings to errors. Keep the compatibility exception local to the
# third-party target, without modifying its managed source.
idf_component_get_property(esp_dl_target espressif__esp-dl COMPONENT_LIB)
if(esp_dl_target AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    target_compile_options(${esp_dl_target} PRIVATE
        -Wno-error=non-c-typedef-for-linkage)
endif()

# IDF 6.1's Bluedroid fixed-size name copies trigger GCC 15's truncation
# diagnostic. Keep the warning visible, scoped to the SDK Bluetooth target.
idf_component_get_property(bt_target bt COMPONENT_LIB)
if(CONFIG_BT_ENABLED AND bt_target AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
    target_compile_options(${bt_target} PRIVATE -Wno-error=stringop-truncation)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
