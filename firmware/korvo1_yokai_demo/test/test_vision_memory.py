"""Protect the validated camera/model memory policy and reject incomplete models."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
model_calls = []
video = (root / 'managed_components/espressif__esp_video/src/device/esp_video_dvp_device.c').read_text()
config = re.search(r'esp_cam_ctlr_dvp_config_t dvp_config = \{(.*?)\n    \};', video, re.S).group(1)
assert '.bk_buffer_dis = true,' in config, 'DVP must reuse application buffers, not allocate another 1.76 MiB frame'
for component in ('human_face_detect', 'human_face_recognition'):
    source = (root / f'managed_components/espressif__{component}/{component}.cpp').read_text()
    calls = re.findall(r'new dl::Model\((.*?)\);', source, re.S)
    assert calls and all(call.rstrip().endswith(', false') for call in calls), 'Keep model parameters in Flash'
    model_calls.extend('delete new dl::Model(' + call + ');' for call in calls)

source = (root / 'main/vision_service.cpp').read_text()
assert 'static bool model_tensors_ready(' in source, 'Reject failed tensor allocations before entering detector inference'
start = source.index('static bool model_tensors_ready(')
function = source[start:source.index('\n}\n', start) + 3]
start = source.index('                if (!model_tensors_ready(s_face_detect->get_raw_model(0))')
guard = source[start:source.index('\n                }', start) + len('\n                }')]
harness = r'''
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstdint>
namespace fbs { enum model_location_type_t { MODEL_LOCATION_IN_FLASH_RODATA, MODEL_LOCATION_IN_SDCARD }; }
namespace dl {
enum memory_manager_t { MEMORY_MANAGER_GREEDY };
struct TensorBase { void *data; };
struct Model {
    Model() = default;
    MODEL_CONSTRUCTORS
    std::map<std::string, TensorBase *> inputs, outputs;
    auto &get_inputs() { return inputs; }
    auto &get_outputs() { return outputs; }
};
}
struct Detector {
    dl::Model *models[2];
    dl::Model *get_raw_model(int index) { return models[index]; }
};
static Detector *s_face_detect;
static bool s_capture_running, s_infer_running;
static int s_state, inference_calls;
#define TAG "test"
#define ESP_LOGE(...) ((void)0)
#define VISION_STATE_ERROR 3
#define CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION 0
#define CONFIG_HUMAN_FACE_FEAT_MODEL_LOCATION 0
static void check_model_constructors() {
    const char *path = "flash", *model_name = "face";
    std::string sd_path = "sdcard";
    MODEL_CALLS
}
'''
checks = r'''
int main() {
    check_model_constructors();
    dl::Model m; dl::TensorBase valid{&m}, failed{nullptr};
    assert(!model_tensors_ready(nullptr));
    assert(!model_tensors_ready(&m));
    m.inputs["input"] = &valid; assert(!model_tensors_ready(&m));
    m.outputs["output"] = &valid; assert(model_tensors_ready(&m));
    m.inputs["input"] = nullptr; assert(!model_tensors_ready(&m));
    m.inputs["input"] = &failed; assert(!model_tensors_ready(&m));
    m.inputs["input"] = &valid;
    m.outputs["other"] = &failed; assert(!model_tensors_ready(&m));
    m.outputs["other"] = nullptr; assert(!model_tensors_ready(&m));
    m.outputs.erase("other");
    Detector detector{{&m, nullptr}}; s_face_detect = &detector;
    s_capture_running = s_infer_running = true; attempt_inference();
    assert(s_capture_running && !s_infer_running && s_state == VISION_STATE_ERROR);
    assert(inference_calls == 0);
    detector.models[1] = &m; s_capture_running = s_infer_running = true;
    attempt_inference(); assert(inference_calls == 1);
}
'''
header = (root / 'managed_components/espressif__esp-dl/dl/model/include/dl_model_base.hpp').read_text()
constructors = re.findall(r'Model\(const char \*[\s\S]*?;', header)
assert len(constructors) == 3
harness = harness.replace('MODEL_CONSTRUCTORS', '\n'.join(c[:-1] + ' { assert(!param_copy); }' for c in constructors))
harness = harness.replace('MODEL_CALLS', '\n'.join(model_calls))
with tempfile.TemporaryDirectory() as tmp:
    c, binary = Path(tmp) / 'check.cpp', Path(tmp) / 'check'
    c.write_text(harness + function + 'static void attempt_inference() { do {\n' +
                 guard + '\n++inference_calls; } while(false); }\n' + checks)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Vision memory policy and failed-model checks passed')
