#include "editor.h"
#include <thread>

namespace waves {


AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path, bool async) {

    PLOG_INFO << "Decoding audio from file " << path << " (name '" << name << "')";
    PLOG_INFO << "Async: " << async;

    AudioDecoder decoder(path);

    AudioSourcePtr result = std::make_shared<AudioSource>(name, path);

    auto decode = [](AudioSourcePtr source) {
        AudioDecoder decoder(source->path);

        // busy flag
        source->loading.store(true);

        // trying to decode
        std::optional<std::vector<audio_sample_t>> pcmData = decoder.decode();

        // on success setting valid flag and computing peaks cache
        if (pcmData) {
            source->pcmData = std::move(*pcmData);
            source->valid = true;
            PLOG_INFO << "Building peaks cache...";
            source->cache.build(source->pcmData, INNER_CHANNELS);
        }

        // not busy
        source->loading.store(false);
    };

    if (async) {
        std::thread decoder_thread(decode, result);
        decoder_thread.detach();
    } else {
        decode(result);
    }

    return result;
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

        tl_view.DrawTimeline(playback_state);

    }
    ImGui::End();

    // =================== MEDIA POOL =====================
    if (ImGui::Begin("Media pool")) {

        mp_view.Draw(*this);

    }

    ImGui::End(); // media pool

    // =================== EXPORT    =====================
    DrawExport();

    // =================== FFT analyzer ==================

    if (tl_view.analyzer.open) {
        ImGui::Begin("Spectrum", &tl_view.analyzer.open);
        tl_view.analyzer.DrawAnalyzed();
        ImGui::End();
    }
}



} // namespace waves
