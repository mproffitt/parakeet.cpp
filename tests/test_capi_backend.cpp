// Device selection and backend release through the C API.
//
// parakeet_capi_set_device picks the device the process backend is created on,
// in place of PARAKEET_DEVICE, and parakeet_capi_release_backend frees that
// backend (and the device memory its graph buffers hold) once no context is
// loaded. Both refuse while a context is loaded: its weights live on the
// backend's device.
//
// The device checks run on any build. With PARAKEET_TEST_SILERO_GGUF set, a
// Silero context checks the refusals, and its VAD result's "backend" field shows
// the device the model ran on. On a GPU build it also checks that the default
// device is used again after a release.
//
// Env:
//   PARAKEET_TEST_SILERO_GGUF   Silero VAD weights (the model checks are skipped
//                               if unset)
#include "parakeet_capi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The device a Silero context ran on, from its VAD result's "backend" field.
static std::string ran_on(parakeet_ctx* ctx) {
    std::vector<float> pcm(16000, 0.0f);
    char* j = parakeet_capi_vad_pcm_json(ctx, pcm.data(), (int)pcm.size(), 16000, nullptr);
    if (!j) return "error: " + std::string(parakeet_capi_last_error(ctx));
    const std::string s = j;
    parakeet_capi_free_string(j);
    const std::string key = "\"backend\":\"";
    const size_t at = s.find(key);
    if (at == std::string::npos) return "no backend in " + s;
    const size_t from = at + key.size();
    return s.substr(from, s.find('"', from) - from);
}

static void device_checks() {
    check(parakeet_capi_set_device("cpu") == 0, "set_device(\"cpu\")");
    check(parakeet_capi_set_device("CPU") == 0, "set_device(\"CPU\")");
    check(parakeet_capi_set_device(nullptr) == 0, "set_device(NULL)");
    check(parakeet_capi_set_device("") == 0, "set_device(\"\")");

    check(parakeet_capi_set_device("definitely-not-a-real-device-9000") != 0,
          "set_device refuses an unknown device");
    check(std::strstr(parakeet_capi_load_error(), "definitely-not-a-real-device-9000") != nullptr,
          "the refusal names the device");

    check(parakeet_capi_release_backend() == 0, "release_backend with nothing loaded");
    check(parakeet_capi_release_backend() == 0, "release_backend twice");

    check(parakeet_capi_set_device("cpu") == 0, "set_device(\"cpu\") for default_device");
    check(std::strcmp(parakeet_capi_default_device(), "cpu") == 0,
          "default_device follows set_device(\"cpu\")");
    check(parakeet_capi_set_device(nullptr) == 0, "set_device(NULL) for default_device");
    setenv("PARAKEET_DEVICE", "cpu", 1);
    check(std::strcmp(parakeet_capi_default_device(), "cpu") == 0,
          "default_device honours PARAKEET_DEVICE=cpu");
    setenv("PARAKEET_DEVICE", "definitely-not-a-real-device-9000", 1);
    check(std::strcmp(parakeet_capi_default_device(), "cpu") == 0,
          "an unknown PARAKEET_DEVICE reads as the cpu fallback");
    unsetenv("PARAKEET_DEVICE");
    std::printf("default device: %s\n", parakeet_capi_default_device());
}

static void model_checks(const char* path) {
    check(parakeet_capi_set_device("cpu") == 0, "set_device(\"cpu\") before load");
    parakeet_ctx* ctx = parakeet_capi_load(path);
    if (!ctx) {
        std::fprintf(stderr, "FAIL: cannot load %s: %s\n", path, parakeet_capi_load_error());
        ++failures;
        return;
    }
    check(ran_on(ctx) == "cpu", "the model runs on the CPU");

    check(parakeet_capi_release_backend() != 0, "release_backend refuses while a context is loaded");
    check(std::strlen(parakeet_capi_load_error()) > 0, "the release refusal says why");
    check(parakeet_capi_set_device(nullptr) != 0, "set_device refuses while a context is loaded");
    check(ran_on(ctx) == "cpu", "the loaded model still runs on the CPU");

    parakeet_capi_free(ctx);
    check(parakeet_capi_release_backend() == 0, "release_backend once the context is freed");

    // The default device: the first GPU, or the CPU on a CPU-only build.
    check(parakeet_capi_set_device(nullptr) == 0, "set_device(NULL) after free");
    ctx = parakeet_capi_load(path);
    check(ctx != nullptr, "load on the default device");
    if (ctx) {
        const std::string dev = ran_on(ctx);
        std::printf("default device: %s\n", dev.c_str());
        check(dev.rfind("error", 0) != 0, "the model runs on the default device");
        parakeet_capi_free(ctx);
    }
    check(parakeet_capi_release_backend() == 0, "release_backend after the second free");
}

int main() {
    device_checks();
    const char* path = std::getenv("PARAKEET_TEST_SILERO_GGUF");
    if (!path || !*path) {
        std::printf("PARAKEET_TEST_SILERO_GGUF unset: model checks skipped\n");
        return failures == 0 ? 0 : 1;
    }
    model_checks(path);
    return failures == 0 ? 0 : 1;
}
