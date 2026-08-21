#include "editor.h"
#include "ImGuiFileDialog.h"
#include "common.h"
#include "effects/biquad_filter.h"
#include "effects/fft_equalizer.h"
#include "effects/fft_analyzer.h"
#include "effects/reverb.h"
#include <fstream>
#include <imgui.h>
#include "nlohmann/json.hpp"
#include "serialization.h"
#include <memory>
#include <optional>
#include <stdexcept>

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
                project_import_export_state.StartOpen();
            }

            if (ImGui::MenuItem("Export project")) {
                project_import_export_state.StartExport();
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

    DrawProjectImportExport();
}

void Editor::initPlugins() {
    plugin_manager.addPlugin(std::make_unique<BiquadFactory>());
    plugin_manager.addPlugin(std::make_unique<FFT_EqualizerFactory>());
    plugin_manager.addPlugin(std::make_unique<FFT_AnalyzerFactory>());
    plugin_manager.addPlugin(std::make_unique<ReverbFactory>());
}

void Editor::SaveProject(std::ofstream& output) {
    //stub
    json project;
    ProjectWriter writer(project);
    writer.write("name", std::string(KNEE_SOUND_PROJECT_TYPE));
    writer.write("version", KNEE_SOUND_PROJECT_FORMAT);

    timeline.serialize(writer.nest("timeline"));

    tl_view.serialize(writer.nest("timeline_view"));
    
    output << project.dump(2);
}

void Editor::OpenProject(std::ifstream& input) {
    using json = nlohmann::json;

    json project;
    try {
        project = json::parse(input);
    } catch (json::parse_error& err) {
        throw std::runtime_error(std::string("Unable to parse: ") + err.what());
    }

    ProjectContext ctx;
    ProjectReader reader(project, ctx);

    // header check
    if (reader.read<std::string>("name", "") != KNEE_SOUND_PROJECT_TYPE) {
        throw std::runtime_error("Fromat name mismatch");
    }

    if (reader.read("version", -1 ) != KNEE_SOUND_PROJECT_FORMAT ) {
        throw std::runtime_error("Format version mismatch");
    }

    // timeline
    if (auto tl_reader = reader.nest("timeline")) {
        timeline.deserialize(*tl_reader);
    }

    // Filling media pool
    media_pool.clear();
    for (auto& [path, audio_src]: ctx.media_map) {
        media_pool.push_back(audio_src);
    }

}

void Editor::ProjectImportExport::StartOpen() {
    PLOG_INFO << "Starting project import";
    mode = Import;
    show_file_choose = true;
}

void Editor::ProjectImportExport::StartExport() {
    PLOG_INFO << "Starting project export";
    mode = Export;
    if (knee_sound_project_path.empty())
        show_file_choose = true;
}

static std::optional<std::string> projectFileChoose(std::string label, std::string dialog_key) {

    std::optional<std::string> path = std::nullopt;
    
    IGFD::FileDialogConfig config;
    config.path = "."; // starting from current directory;
    config.countSelectionMax = 1; // selecting 1 file
    ImGuiFileDialog::Instance()->OpenDialog(dialog_key, label, ".ksp", config);

    if (ImGuiFileDialog::Instance()->Display(dialog_key, ImGuiWindowFlags_NoCollapse, ImVec2(30, 50))) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK

            path = ImGuiFileDialog::Instance()->GetFilePathName();
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }

    return path;
}

void Editor::DrawProjectImportExport() {

    ProjectImportExport &s = project_import_export_state;
    using Mode = ProjectImportExport::Mode;
    // File choose dialog
    // **Required** if show_file_choose is set
    if (s.show_file_choose) {

        std::string label = (s.mode == Mode::Import) ? "Open project" : "Save project";
        std::string key   = (s.mode == Mode::Import) ? "ChooseProjectImportPathKey" 
                                                     : "ChooseProjectExportPathKey";
        std::optional<std::string> path = 
            projectFileChoose(label, key);

        if (path) {
            s.knee_sound_project_path.assign(std::move(*path));
            s.show_file_choose = false;
        } else {
            return;
        }
    }

    // Import/export error popup
    if (ImGui::BeginPopupModal(s.popup_key)) {
        ImGui::TextWrapped("%s", s.popup_msg.c_str());

        if (ImGui::Button("Ok")) {
            ImGui::CloseCurrentPopup();
            s.mode = Mode::None;
        }
        ImGui::EndPopup();
    }

    if (s.mode == Mode::Import) {
        if (s.knee_sound_project_path.empty()) {
            return;
        }
        PLOG_INFO << "Opening project from " << s.knee_sound_project_path;

        std::ifstream project_file(s.knee_sound_project_path);
        if (!project_file.good()) {
            ImGui::OpenPopup(s.popup_key);
            s.popup_msg = "Failed to open " + s.knee_sound_project_path.string();
            PLOG_ERROR << s.popup_msg; 
            return;
        }

        try {
            OpenProject(project_file);
            PLOG_INFO << "Successfully opened from " << s.knee_sound_project_path;
            s.mode = Mode::None; 
        } catch (const std::runtime_error& err)  {
            ImGui::OpenPopup(s.popup_key);
            s.popup_msg = err.what();
            PLOG_ERROR << "Bad project file " << s.knee_sound_project_path << "\n\tMsg:" << s.popup_msg;
        } 

    } else if (s.mode == Mode::Export) {
        if (s.knee_sound_project_path.empty()) {
            return;
        }
        PLOG_INFO << "Saving project to " << s.knee_sound_project_path;
    
        std::ofstream project_file(s.knee_sound_project_path);
        if (!project_file.good()) {
            ImGui::OpenPopup(s.popup_key);
            s.popup_msg = "Failed to open " + s.knee_sound_project_path.string();
            PLOG_ERROR << s.popup_msg;
            return;
        }
    
        Editor::SaveProject(project_file);
    
        PLOG_INFO << "Successfully saved project to " << s.knee_sound_project_path;
        s.mode = Mode::None;
    } else {
        return;
    }
}

} // namespace waves
