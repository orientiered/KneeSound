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

void Editor::DrawExport() {
    if (!show_export_window) return;

    if (ImGui::Begin("Export", &show_export_window)) {

        exporter.Draw(*this);

    }

    ImGui::End();
}

void Editor::Draw() {
    // =================== MENU BAR ===========================

    if (ImGui::BeginMainMenuBar())  {
        if (ImGui::BeginMenu("Menu"))
        {

            if (ImGui::MenuItem("Open project")) {
            }

            if (ImGui::MenuItem("Import audio")) {
                
            }

            ImGui::MenuItem("Export", NULL, &show_export_window);

            ImGui::MenuItem("Toggle debug menu", NULL, &g_debug_flags.debug_window);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // =================== MAIN WINDOW ===================
    if (ImGui::Begin("Audio editor", NULL, 0)) {

        tl_view.DrawTimeline(playback_state, timeline);

    }
    ImGui::End();

    // =================== MEDIA POOL =====================
    if (ImGui::Begin("Media pool")) {

        mp_view.Draw(*this);

    }

    ImGui::End(); // media pool

    // =================== EXPORT    =====================
    DrawExport();
}



} // namespace waves
