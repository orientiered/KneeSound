#include "editor.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path) {

    PLOG_INFO << "Decoding audio from file " << path << " (name '" << name << "')";

    AudioDecoder decoder(path);
    std::optional<std::vector<audio_sample_t>> pcmData = decoder.decode();

    AudioSource result = {false, name, path};
    if (pcmData) {
        result.valid = true;
        result.pcmData = std::move(*pcmData);
    }

    return std::make_shared<AudioSource>(result);
}

void Editor::Draw() {
    // =================== MAIN WINDOW ===================
    ImGui::Begin("Audio editor", NULL, 0);

    tl_view.DrawTimeline(playback_state, timeline);

    ImGui::End();

    // =================== MEDIA POOL =====================
    ImGui::Begin("Media pool");

    mp_view.Draw(*this);

    ImGui::End(); // media pool
}



} // namespace waves
