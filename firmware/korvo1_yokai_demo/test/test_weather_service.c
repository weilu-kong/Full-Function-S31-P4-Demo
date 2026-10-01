/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_service.h"

static const char *s_sample_sunny_json =
    "{\n"
    "  \"latitude\": 35.7,\n"
    "  \"longitude\": 139.6875,\n"
    "  \"current\": {\n"
    "    \"time\": \"2026-09-14T14:30\",\n"
    "    \"temperature_2m\": 26.2,\n"
    "    \"weather_code\": 0\n"
    "  }\n"
    "}";

static const char *s_sample_rain_json =
    "{\n"
    "  \"current\": {\n"
    "    \"time\": \"2026-09-14T21:30\",\n"
    "    \"temperature_2m\": 18.0,\n"
    "    \"weather_code\": 61\n"
    "  }\n"
    "}";

static const char *s_sample_snow_json =
    "{\n"
    "  \"current\": {\n"
    "    \"time\": \"2026-09-14T08:00\",\n"
    "    \"temperature_2m\": -1.7,\n"
    "    \"weather_code\": 71\n"
    "  }\n"
    "}";

static const char *s_sample_night_json =
    "{\n"
    "  \"current\": {\n"
    "    \"time\": \"2026-09-14T22:00\",\n"
    "    \"temperature_2m\": 19.5,\n"
    "    \"weather_code\": 0,\n"
    "    \"is_day\": 0\n"
    "  }\n"
    "}";

static void test_wmo_code_mapping(void)
{
    assert(weather_map_wmo_code(0) == WEATHER_COND_SUNNY);
    assert(weather_map_wmo_code(1) == WEATHER_COND_SUNNY);
    assert(weather_map_wmo_code(2) == WEATHER_COND_CLOUDY);
    assert(weather_map_wmo_code(3) == WEATHER_COND_CLOUDY);
    assert(weather_map_wmo_code(45) == WEATHER_COND_CLOUDY);
    assert(weather_map_wmo_code(51) == WEATHER_COND_RAINY);
    assert(weather_map_wmo_code(61) == WEATHER_COND_RAINY);
    assert(weather_map_wmo_code(80) == WEATHER_COND_RAINY);
    assert(weather_map_wmo_code(71) == WEATHER_COND_SNOWY);
    assert(weather_map_wmo_code(85) == WEATHER_COND_SNOWY);
    assert(weather_map_wmo_code(95) == WEATHER_COND_THUNDER);
    assert(weather_map_wmo_code(99) == WEATHER_COND_THUNDER);
}

static void test_background_mapping(void)
{
    assert(weather_background_for_condition(WEATHER_COND_SUNNY) == WEATHER_BACKGROUND_SUNNY);
    assert(weather_background_for_condition(WEATHER_COND_CLOUDY) == WEATHER_BACKGROUND_SUNNY);
    assert(weather_background_for_condition(WEATHER_COND_RAINY) == WEATHER_BACKGROUND_RAIN);
    assert(weather_background_for_condition(WEATHER_COND_SNOWY) == WEATHER_BACKGROUND_RAIN);
    assert(weather_background_for_condition(WEATHER_COND_THUNDER) == WEATHER_BACKGROUND_RAIN);
}

static void test_json_parsing(void)
{
    int temp = 0;
    int code = -1;
    bool is_day = false;

    bool ok = weather_parse_open_meteo_json(s_sample_sunny_json, &temp, &code, &is_day);
    assert(ok);
    assert(temp == 26);
    assert(code == 0);
    assert(is_day == true); /* defaults to true when omitted */

    ok = weather_parse_open_meteo_json(s_sample_rain_json, &temp, &code, &is_day);
    assert(ok);
    assert(temp == 18);
    assert(code == 61);

    ok = weather_parse_open_meteo_json(s_sample_snow_json, &temp, &code, &is_day);
    assert(ok);
    assert(temp == -2);
    assert(code == 71);

    ok = weather_parse_open_meteo_json(s_sample_night_json, &temp, &code, &is_day);
    assert(ok);
    assert(temp == 20);
    assert(code == 0);
    assert(is_day == false);

    ok = weather_parse_open_meteo_json("{\"invalid\": 123}", &temp, &code, &is_day);
    assert(!ok);

    ok = weather_parse_open_meteo_json("not json", &temp, &code, &is_day);
    assert(!ok);
}

static void test_rejects_invalid_numbers(void)
{
    const char *invalid[] = {
        "{\"current\":{\"temperature_2m\":\"26\",\"weather_code\":0}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":null}}",
        "{\"current\":{\"temperature_2m\":1e999,\"weather_code\":0}}",
        "{\"current\":{\"temperature_2m\":-101,\"weather_code\":0}}",
        "{\"current\":{\"temperature_2m\":101,\"weather_code\":0}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":-1}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":100}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":4}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":61.5}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":0,\"is_day\":null}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":0,\"is_day\":2}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":0,\"is_day\":0.5}}",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":0,\"is_day\":\"0\"}}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int temp = 123, code = 123;
        bool day = false;
        assert(!weather_parse_open_meteo_json(invalid[i], &temp, &code, &day));
        assert(temp == 123 && code == 123 && !day);
        assert(!weather_parse_open_meteo_json(invalid[i], &temp, &code, NULL));
    }
}

static void test_refresh_state(void)
{
    weather_info_t info;
    weather_format_info(&info, true, 26, 0, true, "14:30");
    info.last_success_ms = 1000;
    assert(weather_info_is_fresh(&info, 1000));
    assert(weather_info_is_fresh(&info, 1000 + 15 * 60 * 1000 - 1));
    assert(!weather_info_is_fresh(&info, 1000 + 15 * 60 * 1000));
    assert(!weather_info_is_fresh(&info, 999));
    weather_set_refresh_state(&info, true, false);
    assert(info.refreshing && !info.refresh_failed && info.has_last_success);
    weather_set_refresh_state(&info, false, true);
    assert(!info.refreshing && info.refresh_failed && !info.is_live);
    assert(info.has_last_success && info.temp_c == 26);
    assert(strcmp(info.badge, "STALE") == 0);
    assert(strcmp(info.update_time, "14:30") == 0);
    assert(!weather_info_is_fresh(&info, 1000));
    weather_set_refresh_state(&info, true, false);
    assert(info.refreshing && info.refresh_failed && !weather_info_is_fresh(&info, 1000));
    weather_format_info(&info, false, 26, 0, true, "14:30");
    weather_set_refresh_state(&info, false, true);
    assert(!info.has_last_success && !info.is_live && info.refresh_failed);
}

static void test_formatting_and_lore(void)
{
    weather_info_t info;

    /* Live sunny Tokyo (Daytime) */
    weather_format_info(&info, true, 26, 0, true, "14:30");
    assert(info.is_live == true);
    assert(info.has_last_success);
    assert(info.is_day == true);
    assert(info.condition == WEATHER_COND_SUNNY);
    assert(info.temp_c == 26);
    assert(strcmp(info.badge, "LIVE") == 0);
    assert(strstr(info.location, "東京") != NULL);
    assert(strstr(info.main_text, "晴れ　26℃　更新　14:30") != NULL);
    assert(strstr(info.lore_text, "村の上には、青い空と雲の行列。") != NULL);

    /* Live clear Tokyo (Nighttime) */
    weather_format_info(&info, true, 19, 0, false, "22:00");
    assert(info.is_live == true);
    assert(info.is_day == false);
    assert(info.condition == WEATHER_COND_SUNNY);
    assert(strstr(info.main_text, "晴れ（夜）　19℃　更新　22:00") != NULL);
    assert(strstr(info.lore_text, "夜の妖怪村、提灯が灯り、静かに暮れる。") != NULL);

    /* Live rainy (Daytime) */
    weather_format_info(&info, true, 18, 61, true, "11:30");
    assert(info.is_live == true);
    assert(info.condition == WEATHER_COND_RAINY);
    assert(strstr(info.main_text, "雨　18℃　更新　11:30") != NULL);
    assert(strstr(info.lore_text, "しとしと降る雨と提灯の灯り。") != NULL);

    weather_format_info(&info, true, 26, 0, true, NULL);
    assert(strcmp(info.update_time, "--:--") == 0);

    /* DEMO fallback */
    weather_format_info(&info, false, 26, 0, true, "09:41");
    assert(info.is_live == false);
    assert(strcmp(info.badge, "DEMO") == 0);
    assert(strstr(info.main_text, "晴れ　26℃　更新　09:41") != NULL);
}

int main(void)
{
    test_wmo_code_mapping();
    test_background_mapping();
    test_json_parsing();
    test_rejects_invalid_numbers();
    test_formatting_and_lore();
    test_refresh_state();
    printf("All weather service unit tests passed.\n");
    return 0;
}
