#include "editor.h"
#include "common.h"
#include "effects/biquad_filter.h"
#include "effects/fft_equalizer.h"
#include "effects/fft_analyzer.h"
#include "effects/reverb.h"
#include <memory>

namespace waves {

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

}

void Editor::initPlugins() {
    plugin_manager.addPlugin(std::make_unique<BiquadFactory>());
    plugin_manager.addPlugin(std::make_unique<FFT_EqualizerFactory>());
    plugin_manager.addPlugin(std::make_unique<FFT_AnalyzerFactory>());
    plugin_manager.addPlugin(std::make_unique<ReverbFactory>());
}

} // namespace waves
