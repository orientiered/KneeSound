#include "SFML/Graphics/RenderWindow.hpp"
#include "SFML/System/Clock.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics.hpp"

#include "imgui-SFML.h"
#include "imgui.h"

#include <iostream>
#include <optional>

#include "miniaudio.h"

int main() {
    sf::RenderWindow window(sf::VideoMode::getFullscreenModes()[0], "ImGui + SFML = <3");
    window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(window))
        return -1;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    sf::CircleShape shape(100.f);
    shape.setFillColor(sf::Color::Green);

    sf::Clock deltaClock;

    ma_result result;
    ma_engine engine;

    result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine.\n";
        return -1;
    }

    // ImGui::DockSpaceOverViewport();

    while (window.isOpen())
    {
        while (const auto event = window.pollEvent())
        {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        // ImGui::ShowDemoWindow();

        char audio_file[256];

        ImGui::Begin("Media pool");
        ImGui::InputText("FileName", audio_file, 256);
        if (ImGui::Button("Play")) {
            ma_engine_stop(&engine);
            ma_engine_play_sound(&engine, audio_file, NULL);
        }
        ImGui::Text("%llu", ma_engine_get_time_in_pcm_frames(&engine));
        ImGui::End();


        window.clear();
        window.draw(shape);
        ImGui::SFML::Render(window);
        window.display();
    }

    ma_engine_uninit(&engine);
    ImGui::SFML::Shutdown();
}
