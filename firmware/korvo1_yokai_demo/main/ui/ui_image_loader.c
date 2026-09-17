#include "ui_image_loader.h"
#include "ui_home.h"
#include "ui_weather.h"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ui_image_loader";

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

    uint8_t *out_buf = (uint8_t *)heap_caps_aligned_alloc(16, out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        ESP_LOGE(TAG, "[%s] Failed to allocate %d bytes in PSRAM", name, out_len);
        jpeg_dec_close(dec);
        return ESP_ERR_NO_MEM;
    }

    io.outbuf = out_buf;
    ret = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);

    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "[%s] jpeg_dec_process failed: %d", name, ret);
        heap_caps_free(out_buf);
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

    ret = decode_jpeg_to_dsc("ui_img_weather_sunny", ui_img_weather_sunny_jpg, ui_img_weather_sunny_jpg_len, &ui_img_weather_sunny);
    if (ret != ESP_OK) return ret;

    ret = decode_jpeg_to_dsc("ui_img_weather_cloudy", ui_img_weather_cloudy_jpg, ui_img_weather_cloudy_jpg_len, &ui_img_weather_cloudy);
    if (ret != ESP_OK) return ret;

    ret = decode_jpeg_to_dsc("ui_img_weather_rain", ui_img_weather_rain_jpg, ui_img_weather_rain_jpg_len, &ui_img_weather_rain);
    if (ret != ESP_OK) return ret;

    ret = decode_jpeg_to_dsc("ui_img_weather_night", ui_img_weather_night_jpg, ui_img_weather_night_jpg_len, &ui_img_weather_night);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "All 6 background images successfully decompressed into PSRAM");
    return ESP_OK;
}
