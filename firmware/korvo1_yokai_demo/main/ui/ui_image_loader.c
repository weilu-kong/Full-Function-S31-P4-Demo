#include "ui_image_loader.h"
#include "ui_home.h"
#include "ui_weather.h"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "misc/cache/lv_cache.h"

static const char *TAG = "ui_image_loader";
static int s_weather_background = -1;

/* Raw embedded JPEG buffers defined in ui_img_*.c */
extern const uint8_t ui_img_home_p1_jpg[];
extern const size_t ui_img_home_p1_jpg_len;

extern const uint8_t ui_img_home_p2_jpg[];
extern const size_t ui_img_home_p2_jpg_len;

extern const uint8_t ui_img_weather_sunny_jpg[];
extern const size_t ui_img_weather_sunny_jpg_len;

extern const uint8_t ui_img_weather_cloudy_jpg[];
extern const size_t ui_img_weather_cloudy_jpg_len;

extern const uint8_t ui_img_weather_rain_jpg[];
extern const size_t ui_img_weather_rain_jpg_len;

extern const uint8_t ui_img_weather_night_jpg[];
extern const size_t ui_img_weather_night_jpg_len;

extern const uint8_t ui_img_calculator_bg_jpg[];
extern const size_t ui_img_calculator_bg_jpg_len;

extern const uint8_t ui_img_clock_bg_jpg[];
extern const size_t ui_img_clock_bg_jpg_len;

extern const uint8_t ui_img_fireworks_bg_jpg[];
extern const size_t ui_img_fireworks_bg_jpg_len;

lv_image_dsc_t ui_app_shared_bg = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 800,
        .h = 480,
        .stride = 1600,
    },
    .data_size = 800 * 480 * 2,
    .data = NULL,
};

static ui_app_bg_t s_current_app_bg = UI_APP_BG_NONE;

static esp_err_t decode_jpeg_to_dsc(const char *name, const uint8_t *jpg_data, size_t jpg_len, lv_image_dsc_t *dsc)
{
    if (!jpg_data || jpg_len == 0 || !dsc) {
        return ESP_ERR_INVALID_ARG;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t dec = NULL;
    jpeg_error_t ret = jpeg_dec_open(&config, &dec);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "[%s] jpeg_dec_open failed: %d", name, ret);
        return ESP_FAIL;
    }

    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)jpg_data,
        .inbuf_len = (int)jpg_len,
    };
    jpeg_dec_header_info_t info;
    ret = jpeg_dec_parse_header(dec, &io, &info);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "[%s] jpeg_dec_parse_header failed: %d", name, ret);
        jpeg_dec_close(dec);
        return ESP_FAIL;
    }

    int out_len = 0;
    ret = jpeg_dec_get_outbuf_len(dec, &out_len);
    if (ret != JPEG_ERR_OK || out_len <= 0) {
        out_len = info.width * info.height * 2;
    }

    uint8_t *out_buf = (uint8_t *)dsc->data;
    bool allocated = false;
    if (out_buf) {
        if (dsc->data_size < (uint32_t)out_len) {
            ESP_LOGE(TAG, "[%s] Existing buffer is too small (%u < %d)", name,
                     (unsigned)dsc->data_size, out_len);
            jpeg_dec_close(dec);
            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        out_buf = (uint8_t *)heap_caps_aligned_alloc(16, out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!out_buf) {
            ESP_LOGE(TAG, "[%s] Failed to allocate %d bytes in PSRAM", name, out_len);
            jpeg_dec_close(dec);
            return ESP_ERR_NO_MEM;
        }
        allocated = true;
    }

    io.outbuf = out_buf;
    ret = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);

    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "[%s] jpeg_dec_process failed: %d", name, ret);
        if (allocated) {
            heap_caps_free(out_buf);
        }
        return ESP_FAIL;
    }

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = info.width;
    dsc->header.h = info.height;
    dsc->header.stride = info.width * 2;
    dsc->data_size = (uint32_t)out_len;
    dsc->data = out_buf;

    ESP_LOGI(TAG, "Decoded %s (%dx%d, %u KB jpg -> %d KB PSRAM)",
             name, info.width, info.height, (unsigned)(jpg_len / 1024), out_len / 1024);
    return ESP_OK;
}

esp_err_t ui_images_init(void)
{
    if (ui_img_home_p1.data != NULL) {
        return ESP_OK; /* Already initialized */
    }

    ESP_LOGI(TAG, "Decompressing Yokai background JPEGs into PSRAM via hardware accelerator...");

    esp_err_t ret;

    ret = decode_jpeg_to_dsc("ui_img_home_p1", ui_img_home_p1_jpg, ui_img_home_p1_jpg_len, &ui_img_home_p1);
    if (ret != ESP_OK) return ret;

    ret = decode_jpeg_to_dsc("ui_img_home_p2", ui_img_home_p2_jpg, ui_img_home_p2_jpg_len, &ui_img_home_p2);
    if (ret != ESP_OK) return ret;

    /* Decode the initial weather background; later states reuse this 750KB buffer. */
    ret = decode_jpeg_to_dsc("ui_img_weather_sunny", ui_img_weather_sunny_jpg, ui_img_weather_sunny_jpg_len, &ui_img_weather_sunny);
    if (ret != ESP_OK) return ret;

    s_weather_background = 0;

    ESP_LOGI(TAG, "Background images decompressed (shared weather buffer: saved 2.3MB PSRAM)");
    return ESP_OK;
}

esp_err_t ui_weather_background_load(weather_cond_t condition, bool is_day)
{
    int background = 0;
    const char *name = "ui_img_weather_sunny";
    const uint8_t *jpg = ui_img_weather_sunny_jpg;
    size_t jpg_len = ui_img_weather_sunny_jpg_len;

    if (condition == WEATHER_COND_RAINY || condition == WEATHER_COND_THUNDER) {
        background = 2;
        name = "ui_img_weather_rain";
        jpg = ui_img_weather_rain_jpg;
        jpg_len = ui_img_weather_rain_jpg_len;
    } else if (condition == WEATHER_COND_SNOWY || !is_day) {
        background = 3;
        name = "ui_img_weather_night";
        jpg = ui_img_weather_night_jpg;
        jpg_len = ui_img_weather_night_jpg_len;
    } else if (condition == WEATHER_COND_CLOUDY) {
        background = 1;
        name = "ui_img_weather_cloudy";
        jpg = ui_img_weather_cloudy_jpg;
        jpg_len = ui_img_weather_cloudy_jpg_len;
    }

    if (background == s_weather_background) {
        return ESP_OK;
    }

    esp_err_t ret = decode_jpeg_to_dsc(name, jpg, jpg_len, &ui_img_weather_sunny);
    if (ret == ESP_OK) {
        s_weather_background = background;
        lv_image_cache_drop(&ui_img_weather_sunny);
    }
    return ret;
}

esp_err_t ui_app_background_load(ui_app_bg_t bg)
{
    if (bg == s_current_app_bg && ui_app_shared_bg.data != NULL) {
        return ESP_OK;
    }
    const uint8_t *jpg = NULL;
    size_t len = 0;
    const char *name = NULL;
    switch (bg) {
    case UI_APP_BG_FIREWORKS:
        jpg = ui_img_fireworks_bg_jpg;
        len = ui_img_fireworks_bg_jpg_len;
        name = "ui_img_fireworks_bg";
        break;
    case UI_APP_BG_CLOCK:
        jpg = ui_img_clock_bg_jpg;
        len = ui_img_clock_bg_jpg_len;
        name = "ui_img_clock_bg";
        break;
    case UI_APP_BG_CALCULATOR:
        jpg = ui_img_calculator_bg_jpg;
        len = ui_img_calculator_bg_jpg_len;
        name = "ui_img_calculator_bg";
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = decode_jpeg_to_dsc(name, jpg, len, &ui_app_shared_bg);
    if (ret == ESP_OK) {
        s_current_app_bg = bg;
        lv_image_cache_drop(&ui_app_shared_bg);
    }
    return ret;
}

void ui_app_background_free(void)
{
    if (ui_app_shared_bg.data) {
        lv_image_cache_drop(&ui_app_shared_bg);
        heap_caps_free((void *)ui_app_shared_bg.data);
        ui_app_shared_bg.data = NULL;
        s_current_app_bg = UI_APP_BG_NONE;
        ESP_LOGI(TAG, "Freed shared app background buffer (750KB returned to PSRAM)");
    }
}

