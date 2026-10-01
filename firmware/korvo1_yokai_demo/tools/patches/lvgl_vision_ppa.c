/* Live preview only: inference retains its three 320x240 buffers. */
static void lv_draw_ppa_vision_scale(lv_draw_task_t *t, const lv_draw_image_dsc_t *dsc,
                                   const lv_area_t *coords)
{
    lv_layer_t *layer = t->target_layer;
    lv_draw_buf_t *buf = layer->draw_buf;
    lv_draw_ppa_unit_t *u = (lv_draw_ppa_unit_t *)t->draw_unit;
    const lv_image_dsc_t *image = dsc->src;
    const lv_area_t output = {coords->x1, coords->y1, coords->x1 + 399, coords->y1 + 299};
    int x = output.x1 - layer->buf_area.x1;
    int y = output.y1 - layer->buf_area.y1;

    /* SRM writes the whole block: a clipped draw must use LVGL's software path. */
    if(output.x1 < t->clip_area.x1 || output.y1 < t->clip_area.y1 ||
       output.x2 > t->clip_area.x2 || output.y2 > t->clip_area.y2 ||
       output.x1 < layer->buf_area.x1 || output.y1 < layer->buf_area.y1 ||
       output.x2 > layer->buf_area.x2 || output.y2 > layer->buf_area.y2 ||
       x < 0 || y < 0 || x + 400 > (int)buf->header.w || y + 300 > (int)buf->header.h ||
       !image->data || image->header.stride != 640 || image->data_size < 153600 ||
       buf->header.stride % 2 || buf->header.stride < buf->header.w * 2 ||
       (uint64_t)buf->header.stride * buf->header.h > buf->data_size) {
        u->img_sw_fallback = true;
        lv_draw_sw_image(t, dsc, &t->area);
        return;
    }

    ppa_srm_oper_config_t cfg = {
        .in = {
            .buffer = image->data,
            .pic_w = 320, .pic_h = 240,
            .block_w = 320, .block_h = 240,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = buf->data, .buffer_size = buf->data_size,
            .pic_w = buf->header.stride / 2, .pic_h = buf->header.h,
            .block_offset_x = x, .block_offset_y = y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .scale_x = 1.25f, .scale_y = 1.25f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    int ret = ppa_do_scale_rotate_mirror(u->srm_client, &cfg);
    if(ret != ESP_OK) {
        LV_LOG_WARN("Vision PPA scaling failed: %d", ret);
        u->img_sw_fallback = true;
        lv_draw_sw_image(t, dsc, &t->area);
        return;
    }
    static bool logged;
    if(!logged) {
        LV_LOG_USER("[VISION_PPA] input=320x240 output=400x300 extra_fb=0");
        logged = true;
    }
}
