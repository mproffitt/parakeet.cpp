// parakeet_capi_probe_json: a GGUF's model kind from its header alone. No model
// file is needed: the test writes header-only GGUF files itself.
#include <cstdio>
#include <string>
#include <unistd.h>

#include "gguf.h"
#include "parakeet_capi.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

namespace {

std::string header(const std::string& name, const std::string& key, const std::string& value) {
    const std::string path = "test_capi_probe." + name + ".gguf";
    gguf_context* g = gguf_init_empty();
    if (!key.empty()) gguf_set_val_str(g, key.c_str(), value.c_str());
    gguf_write_to_file(g, path.c_str(), /*only_meta=*/false);
    gguf_free(g);
    return path;
}

std::string probe(const std::string& path) {
    char* j = parakeet_capi_probe_json(path.c_str());
    std::string s = j ? j : "";
    parakeet_capi_free_string(j);
    return s;
}

}  // namespace

int main() {
    const std::string vad = header("silero", "general.architecture", "silero_vad");
    const std::string vad_only = header("vadonly", "parakeet.arch", "vad");
    const std::string diar = header("diar", "parakeet.arch", "diarization");
    const std::string sound = header("ced", "general.architecture", "ced");
    const std::string speaker = header("voice", "general.architecture", "voicedetect");
    const std::string asr = header("tdt", "parakeet.arch", "tdt");
    const std::string none = header("none", "", "");
    const std::string bad_bundle = header("bundle", "general.architecture", "parakeet-bundle");

    CHECK(probe(vad) == R"({"kind":"vad"})");
    CHECK(probe(vad_only) == R"({"kind":"vad"})");
    CHECK(probe(diar) == R"({"kind":"diarization"})");
    CHECK(probe(sound) == R"({"kind":"sound"})");
    CHECK(probe(speaker) == R"({"kind":"speaker"})");
    CHECK(probe(asr) == R"({"kind":"asr"})");

    CHECK(parakeet_capi_probe_json(none.c_str()) == nullptr);
    CHECK(std::string(parakeet_capi_load_error()).find("not a parakeet model") != std::string::npos);
    CHECK(parakeet_capi_probe_json("test_capi_probe.does-not-exist.gguf") == nullptr);
    CHECK(std::string(parakeet_capi_load_error()).find("cannot read") != std::string::npos);
    // A bundle whose header is malformed fails like parakeet_capi_bundle_components_json.
    CHECK(parakeet_capi_probe_json(bad_bundle.c_str()) == nullptr);
    CHECK(std::string(parakeet_capi_load_error()).find("bundle has no") != std::string::npos);
    CHECK(parakeet_capi_probe_json(nullptr) == nullptr);

    for (const auto& p : {vad, vad_only, diar, sound, speaker, asr, none, bad_bundle}) unlink(p.c_str());
    if (failures == 0) std::printf("test_capi_probe: OK\n");
    return failures == 0 ? 0 : 1;
}
