"""Reject incomplete raw DMA frames without modifying the installed ESP-IDF."""
import sys
from pathlib import Path

source = Path(sys.argv[1]) / "components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c"
text = source.read_text()
marker = "    size_t dma_recv_size = esp_cam_ctlr_dvp_dma_get_recv_size(&ctlr->dma);\n"
assert text.count(marker) == 1, "unexpected camera driver source"
trace = """    static uint32_t frames, short_frames, error_frames;
    static size_t minimum = SIZE_MAX, maximum;
    frames++;
    if (dma_recv_size != ctlr->fb_size_in_bytes) short_frames++;
    for (int i = 0; i < ctlr->dma.desc_count; i++) {
        if (ctlr->dma.desc[i].dw0.err_eof) { error_frames++; break; }
    }
    if (dma_recv_size < minimum) minimum = dma_recv_size;
    if (dma_recv_size > maximum) maximum = dma_recv_size;
    if (frames % 80 == 0) {
        ESP_EARLY_LOGW(TAG, "[DVP_DMA] frames=%lu mismatch=%lu errors=%lu min=%u max=%u expected=%u",
                       (unsigned long)frames, (unsigned long)short_frames,
                       (unsigned long)error_frames,
                       (unsigned)minimum, (unsigned)maximum, (unsigned)ctlr->fb_size_in_bytes);
        minimum = SIZE_MAX;
        maximum = 0;
    }
"""
raw_size = "    uint32_t recv_buffer_size;\n"
assert text.count(raw_size) == 1, "unexpected raw-frame size calculation"
text = text.replace(raw_size, raw_size +
                    "    /* A truncated raw frame must not expose stale pixels as a complete image. */\n"
                    "    if (!ctlr->pic_format_jpeg && dma_recv_size != ctlr->fb_size_in_bytes) {\n"
                    "        return 0;\n"
                    "    }\n")
# When all application buffers are held, recycle the just-completed DMA buffer
# before publishing it. It remains driver-owned, so readers cannot see a rewrite.
replacements = (
    ("esp_cam_ctlr_dvp_start_trans(esp_cam_ctlr_dvp_cam_t *ctlr)",
     "esp_cam_ctlr_dvp_start_trans(esp_cam_ctlr_dvp_cam_t *ctlr, uint8_t *recycle_buffer)"),
    ('    if (!buffer_ready) {\n        assert(false && "no new buffer, and no driver internal buffer");\n    }',
     '    if (!buffer_ready && recycle_buffer) {\n'
     '        trans.buffer = recycle_buffer;\n'
     '        trans.buflen = ctlr->fb_size_in_bytes;\n'
     '        buffer_ready = true;\n'
     '    }\n'
     '    if (!buffer_ready) return ESP_ERR_NOT_FOUND;'),
    ("    esp_cam_ctlr_dvp_start_trans(ctlr);",
     "    esp_cam_ctlr_dvp_start_trans(ctlr, trans.buffer);"),
    ("    if ((trans.buffer != ctlr->backup_buffer) || ctlr->bk_buffer_exposed) {",
     "    if (trans.buffer != ctlr->cur_buf &&\n"
     "        ((trans.buffer != ctlr->backup_buffer) || ctlr->bk_buffer_exposed)) {"),
    ("        ret = esp_cam_ctlr_dvp_start_trans(ctlr);",
     "        ret = esp_cam_ctlr_dvp_start_trans(ctlr, NULL);"),
)
for before, after in replacements:
    assert text.count(before) == 1, "unexpected camera buffer ownership code"
    text = text.replace(before, after)
if "--diagnostics" in sys.argv[3:]:
    text = text.replace(marker, marker + trace)
output = Path(sys.argv[2])
if not output.exists() or output.read_text() != text:
    output.write_text(text)
