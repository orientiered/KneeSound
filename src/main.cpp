#include "SFML/Graphics/RenderWindow.hpp"
#include "SFML/System/Clock.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics.hpp"

#include "imgui-SFML.h"
#include "imgui.h"

#include <iostream>
#include <memory>
#include <optional>

#include "ImGuiFileDialog.h"

#include "common.h"

#include "editor.h"


void handle_media_pool_player(waves::Editor& editor);

void handle_debug_controller();

int main() {
    plog::init<plog::TxtFormatter>(plog::debug, plog::streamStdErr);

    PLOG_INFO << "Starting audio editor";

    sf::RenderWindow window(sf::VideoMode::getFullscreenModes()[0], "Audio editor");
    window.setFramerateLimit(60);

    /* ============== Setting up ImGUI ==================== */
    if (!ImGui::SFML::Init(window, false)) {
        PLOG_FATAL << "Failed to init ImGUI";
        return -1;
    }

    // fonts
    ImGuiIO &imIO = ImGui::GetIO();

    imIO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const char * font_path = "./data/Play-Regular.ttf";

    ImFont* font = imIO.Fonts->AddFontFromFileTTF(font_path, 16.f, nullptr, imIO.Fonts->GetGlyphRangesCyrillic());
    PLOG_ERROR_IF(!font) << "Failed to load font " << font_path;

    if (!font) {
        PLOG_WARNING << "Fallback to default font";
        font = imIO.Fonts->AddFontDefault();
    }

    PLOG_ERROR_IF(!ImGui::SFML::UpdateFontTexture()) << "Failed to update font textures";

    // clock for frame updates
    sf::Clock deltaClock;

    /* ================= Editor init ============ */

    waves::Editor editor;

    PLOG_DEBUG << "Playback state addr:" << &editor.playback_state << "\n";

    while (window.isOpen())
    {
        PLOG_VERBOSE << "Render cycle start";
        // if (font)
        //     ImGui::PushFont(font);


        while (const auto event = window.pollEvent())
        {
            PLOG_VERBOSE << "Processing event";
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>())
            {
                window.close();
                PLOG_NONE << "See you later\n";
            }
        }

        PLOG_VERBOSE << "Calling imgui update";
        ImGui::SFML::Update(window, deltaClock.restart());
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);


        // =================== MAIN WINDOW ===================
        ImGui::Begin("Audio editor", NULL, 0);

        editor.tl_view.DrawTimeline(editor.timeline);

        ImGui::End();

        // =================== MEDIA POOL =====================
        ImGui::Begin("Media pool");

        if (ImGui::Button("Import audio")) {
            IGFD::FileDialogConfig config;
            config.path = "."; // starting from current directory
            config.countSelectionMax = 0; // selecting any number of files

            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".*,.wav,.mp3,.ogg", config);
        }
        // display
        if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
            if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                std::map<std::string, std::string> selection =
                    ImGuiFileDialog::Instance()->GetSelection();

                for (auto [name, path]: selection) {
                    waves::AudioSourcePtr src = waves::decode_audio_from_file(name, path);

                    editor.media_pool.push_back(src);

                    //TODO:REMOVE
                    editor.timeline.tracks[0].addClip(waves::Clip(src, 0));
                }
            }
            // close
            ImGuiFileDialog::Instance()->Close();
        }

        handle_media_pool_player(editor);

        ImGui::End(); // media pool

        // ================== DEBUG INFO ============================
        handle_debug_controller();

        // if (font)
        //     ImGui::PopFont();

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    PLOG_INFO << "Shutting down imgui";

    ImGui::SFML::Shutdown();

    return 0;
}

void handle_debug_controller() {
    static int severity_idx = 0;
    static bool show_imgui_demo = false;

    // ======= Editor Debug  =====
    ImGui::Begin("Debug settings");

    const char * sev_strings[] = {
        "debug", "info", "verbose"
    };

    const plog::Severity sevs[] = {
        plog::debug, plog::info, plog::verbose
    };


    if (ImGui::ListBox("Debug severity", &severity_idx, sev_strings, 3)) {
        plog::get()->setMaxSeverity(sevs[severity_idx]);
    }

    ImGui::Checkbox("Show demo window", &show_imgui_demo);


    ImGui::End();

    // ======== Imgui demo window =========
    if (show_imgui_demo) {
        ImGui::ShowDemoWindow();
    }
}

void handle_media_pool_player(waves::Editor& editor) {
    int track_idx = 0;
    waves::SourceIt eraseIt = editor.media_pool.end();

    for (auto it = editor.media_pool.begin(); it != editor.media_pool.end(); it++, track_idx++) {

        waves::MediaPool &pool = editor.media_pool;
        const waves::AudioSourcePtr src = *it;

        bool playing = editor.playback_state.isPlaying;
        waves::SourceIt currentTrack = editor.playback_state.currentTrack;
        bool on_current = currentTrack == it;


        ImGui::PushID(track_idx);
            if (ImGui::Button("X")) {
                eraseIt = it;
            }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::Text("%s", src->name.c_str());

        if (!src->valid) {

            ImGui::SameLine();
            ImGui::Text("Failed to decode");

        } else {
            const char *button_text =
                (on_current && playing) ? "Stop" : "Play";

            ImGui::SameLine();
            ImGui::PushID(track_idx);
                if (ImGui::Button(button_text)) {
                    if (!on_current) {
                        editor.playback_state.setTrack(it);
                        editor.playback_state.setPlaying(true);
                    } else {
                        editor.playback_state.setPlaying(!playing);
                    }
                }
            ImGui::PopID();

            if (on_current) {
                ImGui::SameLine();
                int slider_frame = editor.playback_state.getCurrentTrackPosInFrames();

                if (ImGui::SliderInt("Frame", &slider_frame, 0, editor.playback_state.getCurrentTrackLenInFrames())) {
                    editor.playback_state.setCurrentTrackPosInFrames(slider_frame);
                }

            }

        }

    }

    if (eraseIt != editor.media_pool.end()) {
        if (editor.playback_state.currentTrack == eraseIt) {
            editor.playback_state.setPlaying(false);
            editor.playback_state.setTrack(editor.media_pool.end());
        }

        editor.media_pool.erase(eraseIt);
    }
}
