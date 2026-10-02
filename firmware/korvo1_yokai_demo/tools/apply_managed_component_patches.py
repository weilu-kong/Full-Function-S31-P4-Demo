#!/usr/bin/env python3
"""Apply required managed-component fixes and diagnostics after dependency resolution."""

import argparse
import re
from pathlib import Path


VISION_PPA = (Path(__file__).parent / "patches/lvgl_vision_ppa.c").read_text()


PATCHES = (
    (
        "managed_components/espressif__esp_video/src/device/esp_video_dvp_device.c",
        "        .pic_format_jpeg = CAPTURE_VIDEO_GET_FORMAT_PIXEL_FORMAT(video) == V4L2_PIX_FMT_JPEG,\n",
        "        .pic_format_jpeg = CAPTURE_VIDEO_GET_FORMAT_PIXEL_FORMAT(video) == V4L2_PIX_FMT_JPEG,\n"
        "        .bk_buffer_dis = true, /* Recycle application buffers with prepare_camera_driver.py. */\n",
        1,
    ),
    (
        "managed_components/espressif__human_face_detect/human_face_detect.cpp",
        "static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));",
        "static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION), 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);",
        3,
    ),
    (
        "managed_components/espressif__human_face_detect/human_face_detect.cpp",
        "new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);",
        "new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);",
        3,
    ),
    (
        "managed_components/espressif__human_face_recognition/human_face_recognition.cpp",
        "static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION));",
        "static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION), 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);",
        1,
    ),
    (
        "managed_components/espressif__human_face_recognition/human_face_recognition.cpp",
        "new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);",
        "new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD, 0, dl::MEMORY_MANAGER_GREEDY, nullptr, false);",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_ppa_accel_v9.c",
        '#include "src/draw/lv_draw.h"\n#include "src/draw/lv_draw_buf.h"',
        '/* Public draw declarations are included by lvgl.h. */',
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_ppa_accel_v9.c",
        '#include "stdlib/lv_mem.h"\n#include "misc/lv_color.h"',
        '/* Public memory/color declarations are included by lvgl.h. */',
        1,
    ),
    (
        "managed_components/espressif__esp-dl/vision/recognition/dl_recognition_database.cpp",
        "    int i = 1;\n    for (auto it = m_feats.begin(); it != m_feats.end(); it++, i++) {\n"
        "        sim = cal_similarity(it->feat, (float *)feat->data);\n"
        "        if (sim <= thr) {\n            continue;\n        }\n"
        "        // results.emplace_back(it->id, sim);\n        results.emplace_back(i, sim);",
        "    for (auto it = m_feats.begin(); it != m_feats.end(); it++) {\n"
        "        sim = cal_similarity(it->feat, (float *)feat->data);\n"
        "        if (sim <= thr) {\n            continue;\n        }\n"
        "        results.emplace_back(it->id, sim);",
        1,
    ),
    (
        "managed_components/espressif__esp-dl/vision/image/dl_image_preprocessor.cpp",
        "    m_model_input = model->get_input(input_name);\n    assert(m_model_input->dtype",
        "    m_model_input = model->get_input(input_name);\n"
        "    if (!m_model_input) {\n"
        "        ESP_LOGE(\"ImagePreprocessor\", \"Failed to get model input tensor '%s'\", input_name.c_str());\n"
        "        return;\n"
        "    }\n"
        "    assert(m_model_input->dtype",
        2,
    ),
    (
        "managed_components/espressif__human_face_recognition/human_face_recognition.hpp",
        "                  bool lazy_load = true);\n\nprivate:",
        "                  bool lazy_load = true);\n"
        "    int get_feat_len() override { return 512; }\n\n"
        "private:",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c",
        "    uint32_t switch_seq;\n    uint32_t double_wait_switch_seq;",
        "    uint32_t switch_seq;\n"
        "    volatile uint32_t frame_complete_count;\n"
        "    uint32_t switch_wait_timeout_count;\n"
        "    uint32_t double_wait_switch_seq;",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c",
        "        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);\n",
        "        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) == 0) {\n"
        "            impl->switch_wait_timeout_count++;\n"
        "            ESP_LOGW(TAG, \"[DISPLAY_STALL] pending_fb=%p disp_fb=%p draw_fb=%p \"\n"
        "                     \"switch_seq=%\" PRIu32 \" frame_complete=%\" PRIu32 \" wait_timeout=%\" PRIu32,\n"
        "                     impl->pending_fb, impl->disp_fb, impl->draw_fb, impl->switch_seq,\n"
        "                     impl->frame_complete_count, impl->switch_wait_timeout_count);\n"
        "        }\n",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c",
        "    BaseType_t need_yield = pdFALSE;\n\n    display_bridge_v9_signal_dummy_draw_event(impl, ESP_LV_ADAPTER_DUMMY_DRAW_EVT_FRAME_DONE, &need_yield);",
        "    BaseType_t need_yield = pdFALSE;\n"
        "    impl->frame_complete_count++;\n\n"
        "    display_bridge_v9_signal_dummy_draw_event(impl, ESP_LV_ADAPTER_DUMMY_DRAW_EVT_FRAME_DONE, &need_yield);",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c",
        "    esp_err_t ret = display_bridge_v9_blit_full_frame(impl, frame_buffer, true);\n"
        "    if (ret != ESP_OK) {\n"
        "        impl->notify_task = prev_notify;\n"
        "        return ret;\n"
        "    }\n\n"
        "    portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "    impl->pending_fb = frame_buffer;\n"
        "    portEXIT_CRITICAL(&impl->pipeline.lock);\n\n"
        "    display_bridge_pipeline_mark_buf_busy(&impl->pipeline, impl->disp_fb);",
        "    void *old_disp_fb = impl->disp_fb;\n"
        "    display_bridge_pipeline_mark_buf_busy(&impl->pipeline, old_disp_fb);\n"
        "    portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "    impl->pending_fb = frame_buffer;\n"
        "    portEXIT_CRITICAL(&impl->pipeline.lock);\n\n"
        "    esp_err_t ret = display_bridge_v9_blit_full_frame(impl, frame_buffer, true);\n"
        "    if (ret != ESP_OK) {\n"
        "        portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "        impl->pending_fb = NULL;\n"
        "        portEXIT_CRITICAL(&impl->pipeline.lock);\n"
        "        display_bridge_pipeline_release_buf_isr(&impl->pipeline);\n"
        "        impl->notify_task = prev_notify;\n"
        "        return ret;\n"
        "    }",
        1,
    ),
    (
        "managed_components/espressif__esp_lvgl_adapter/src/display/bridge/v9/lvgl_bridge_v9.c",
        "    esp_err_t ret = display_bridge_v9_blit_full_frame(impl, color_map, false);\n"
        "    if (ret != ESP_OK) {\n"
        "        ESP_LOGE(TAG, \"Blit failed: %s\", esp_err_to_name(ret));\n"
        "    } else {\n"
        "        /* Old front buffer stays busy until DMA switches to color_map. */\n"
        "        display_bridge_pipeline_mark_buf_busy(&impl->pipeline, impl->disp_fb);\n"
        "        portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "        impl->pending_fb = color_map;\n"
        "        portEXIT_CRITICAL(&impl->pipeline.lock);",
        "    /* Publish ownership before starting the panel switch: the completion ISR may run inline. */\n"
        "    void *old_disp_fb = impl->disp_fb;\n"
        "    display_bridge_pipeline_mark_buf_busy(&impl->pipeline, old_disp_fb);\n"
        "    portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "    impl->pending_fb = color_map;\n"
        "    portEXIT_CRITICAL(&impl->pipeline.lock);\n\n"
        "    esp_err_t ret = display_bridge_v9_blit_full_frame(impl, color_map, false);\n"
        "    if (ret != ESP_OK) {\n"
        "        portENTER_CRITICAL(&impl->pipeline.lock);\n"
        "        impl->pending_fb = NULL;\n"
        "        portEXIT_CRITICAL(&impl->pipeline.lock);\n"
        "        display_bridge_pipeline_release_buf_isr(&impl->pipeline);\n"
        "        ESP_LOGE(TAG, \"Blit failed: %s\", esp_err_to_name(ret));\n"
        "    } else {",
        1,
    ),


    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa.c",
        "    if(!ppa_dest_cf_supported(base->layer->color_format)) return 0;",
        "    if(!ppa_dest_cf_supported(base->layer->color_format)) return 0;\n"
        "    /* Keep other UI drawing on its existing software path. */\n"
        "    if(t->type != LV_DRAW_TASK_TYPE_IMAGE) return 0;",
        1,
    ),
    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa.c",
        "                     && dsc->scale_x == 256\n                     && dsc->scale_y == 256",
        "                     && dsc->scale_x == 320\n                     && dsc->scale_y == 320\n"
        "                     && dsc->header.w == 320 && dsc->header.h == 240\n"
        "                     && dsc->header.cf == LV_COLOR_FORMAT_RGB565\n"
        "                     && base->layer->color_format == LV_COLOR_FORMAT_RGB565\n"
        "                     && dsc->pivot.x == 0 && dsc->pivot.y == 0",
        1,
    ),
    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa.c",
        "                if(t->preference_score > DRAW_UNIT_PPA_PREF_SCORE) {",
        "                /* SRM scales pixel extents, not the distance between corner pixels. */\n"
        "                if(t->type == LV_DRAW_TASK_TYPE_IMAGE) {\n"
        "                    t->_real_area.x2 = t->area.x1 + 399;\n"
        "                    t->_real_area.y2 = t->area.y1 + 299;\n"
        "                }\n"
        "                if(t->preference_score > DRAW_UNIT_PPA_PREF_SCORE) {",
        2,
    ),
    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa.c",
        "    if(!lv_area_intersect(&area, &t->area, &t->clip_area)) return;",
        "    if(!lv_area_intersect(&area, &t->_real_area, &t->clip_area)) return;",
        1,
    ),
    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa_img.c",
        "\nvoid lv_draw_ppa_img(lv_draw_task_t * t,",
        "\n" + VISION_PPA + "\nvoid lv_draw_ppa_img(lv_draw_task_t * t,",
        1,
    ),
    (
        "managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa_img.c",
        "    lv_draw_image_normal_helper(t, dsc, coords, lv_draw_img_ppa_core, NULL);",
        "    if(dsc->scale_x == 320 && dsc->scale_y == 320) {\n"
        "        lv_draw_ppa_vision_scale(t, dsc, coords);\n"
        "        return;\n"
        "    }\n"
        "    lv_draw_image_normal_helper(t, dsc, coords, lv_draw_img_ppa_core, NULL);",
        1,
    ),
)


P4_PATCHES = (
    (
        "managed_components/espressif__esp32_p4_function_ev_board_noglib/esp32_p4_function_ev_board.c",
        ".codec_mode = ESP_CODEC_DEV_TYPE_OUT,",
        ".codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, /* One shared ES8311 ADC/DAC instance. */",
        1,
    ),
    # ponytail: this board registers one SC2336 JSON; pass a CMake list for future multi-sensor profiles.
    (
        "managed_components/espressif__esp_ipa/tools/config/esp_ipa_config.py",
        '        files = input.split()\n',
        '        files = input\n',
        1,
    ),
    (
        "managed_components/espressif__esp_ipa/tools/config/esp_ipa_config.py",
        "        '--input', '-i',\n",
        "        '--input', '-i', nargs='+',\n",
        1,
    ),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--target", default="esp32s31", choices=("esp32s31", "esp32p4"))
    args = parser.parse_args()

    patches = PATCHES + (P4_PATCHES if args.target == "esp32p4" else ())
    for relative, before, after, expected_count in patches:
        path = args.project_root / relative
        if not path.is_file():
            raise SystemExit(f"missing managed component source: {path}")
        text = path.read_text()
        if text.count(after) == expected_count:
            continue
        if args.check:
            raise SystemExit(f"required patch is not applied: {relative}")
        if text.count(before) != expected_count:
            raise SystemExit(f"unexpected upstream source; cannot patch safely: {relative}")
        path.write_text(text.replace(before, after))
        print(f"patched {relative}")
    # project() resolves dependencies and prepares ThorVG's Meson template.
    # Repair response-file argument quoting before CMake's generation phase;
    # this also works on the first configure of a clean dependency download.
    if args.build_root:
        cross_file = args.build_root / "esp-idf/espressif__thorvg/thorvg_build/cross_file.txt.tmp"
        if cross_file.is_file():
            text = cross_file.read_text()
            fixed = re.sub(r"'\"([^']*)\"'", r"'\1'", text)
            if fixed != text:
                if args.check:
                    raise SystemExit("ThorVG Meson arguments contain literal GCC quotes")
                cross_file.write_text(fixed)
                print("patched ThorVG Meson response-file argument quotes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
