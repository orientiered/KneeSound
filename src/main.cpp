#include "SFML/Graphics/RenderWindow.hpp"
#include "SFML/System/Clock.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics.hpp"

#include "common.h"

#include "imgui-SFML.h"
#include "imgui.h"

#include <optional>

#include "editor.h"


void handle_debug_controller();

// GLOBAL DEBUG FLAGS
DebugFlags g_debug_flags;

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

    ImFont* font = imIO.Fonts->AddFontFromFileTTF(font_path, 30.f, nullptr, imIO.Fonts->GetGlyphRangesCyrillic());
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
        PLOG_VERBOSE_IF(g_debug_flags.render_loop_logs) << "Render cycle start";
        // if (font)
        //     ImGui::PushFont(font);


        while (const auto event = window.pollEvent())
        {
            PLOG_VERBOSE_IF(g_debug_flags.render_loop_logs) << "Processing event";
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>())
            {
                window.close();
                PLOG_NONE << "See you later\n";
            }
        }

        PLOG_VERBOSE_IF(g_debug_flags.render_loop_logs) << "Calling imgui update";
        ImGui::SFML::Update(window, deltaClock.restart());
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        // ================== EDITOR DRAW ===========================

       editor.Draw();

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

    ImGui::Checkbox("Log main render loop", &g_debug_flags.render_loop_logs);
    ImGui::Checkbox("Log audio callback", &g_debug_flags.callback_logs);

    ImGui::Checkbox("Show demo window", &show_imgui_demo);


    ImGui::End();

    // ======== Imgui demo window =========
    if (show_imgui_demo) {
        ImGui::ShowDemoWindow();
    }
}


