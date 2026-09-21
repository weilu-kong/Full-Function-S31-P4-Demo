#!/usr/bin/env python3
"""Apply required managed-component fixes and diagnostics after dependency resolution."""

import argparse
from pathlib import Path


PATCHES = (
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
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    for relative, before, after, expected_count in PATCHES:
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
