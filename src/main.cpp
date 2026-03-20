#include "SFML/Graphics/RenderWindow.hpp"
#include "SFML/System/Clock.hpp"
#include "SFML/Window/Event.hpp"
#include "SFML/Graphics.hpp"

#include "imgui-SFML.h"
#include "imgui.h"

#include <iostream>
#include <memory>
#include <optional>

#include "miniaudio.h"
#include "ImGuiFileDialog.h"

#include <plog/Log.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>


struct AudioSource {
    bool valid = false;
    std::string name;
    std::string path;
    std::vector<float> pcmData;

};

using AudioSourcePtr = std::shared_ptr<AudioSource>;

class AudioDecoder {
    ma_decoder decoder;
    bool init = false;

    std::string path_;
    const int channels = 2, sampleRate = 48000;

public:
    AudioDecoder(const std::string& path) {
        path_ = path;

        ma_decoder_config dcd_cfg = ma_decoder_config_init(ma_format_f32, channels, sampleRate);
        ma_result init_res = ma_decoder_init_file(path.c_str(), &dcd_cfg, &decoder);
        if (init_res != MA_SUCCESS) {
            PLOG_NONE << "Failed to decode file " << path;
            init = false;
        } else {
            init = true;
        }
    }

    AudioSourcePtr decode(const std::string& name) {
        AudioSource result{init, name, path_};

        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);

        result.pcmData.resize(totalFrames*channels);

        // Reading
        ma_uint64 framesRead = 0;
        ma_result read_res = ma_decoder_read_pcm_frames(&decoder, result.pcmData.data(), totalFrames, &framesRead);

        if (framesRead < totalFrames) {
            result.valid = true;
            PLOG_WARNING << "When decoding " << path_ << " read " << framesRead << "/" << totalFrames << " of frames";
        }

        result.pcmData.resize(framesRead * channels);

        return std::make_shared<AudioSource>(result);
    }

    ~AudioDecoder() {
        if (init) {
            ma_decoder_uninit(&decoder);
        }
    }
};

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path) {

    PLOG_INFO << "Decoding audio from file " << path << " (name '" << name << "')";

    AudioDecoder decoder(path);

    AudioSourcePtr result = decoder.decode(name);

    return result;
}

// void simplePlayer() {
//     ImGui::SliderFloat(const char *label, float *v, float v_min, float v_max);
// }

using MediaPool = std::list<AudioSourcePtr>;
using SourceIt = typeof(MediaPool().begin());
// using SourceIt = std::_List_iterator<AudioSourcePtr>;

struct PlaybackState {
    bool isPlaying = false;

    int64_t currentFrame = 0;
    SourceIt currentTrack;

    MediaPool& pool;

    std::mutex mtx;

    const int channels = 2;


    PlaybackState(MediaPool& pool_) : pool(pool_) {}

    void getFrames(void *out, ma_uint32 frameCount) {
        mtx.lock();
        if (isPlaying) {
            PLOG_VERBOSE << "getFrames callback: writing " << frameCount << " frames to " << out;

            const std::vector<float>& pcmData = (*currentTrack)->pcmData;
            const size_t trackLen = pcmData.size();

            auto startIt = (channels*currentFrame >= trackLen ) ?
                            pcmData.end() :
                            pcmData.begin() + channels*currentFrame;
            auto endIt = (channels*(currentFrame + frameCount) >= trackLen) ?
                            pcmData.end() :
                            pcmData.begin() + channels*(currentFrame + frameCount);

            std::copy(startIt, endIt, reinterpret_cast<float*>(out));
            currentFrame += frameCount;
        }

        mtx.unlock();
    }

    int32_t getCurrentTrackLenInFrames() {
        return (*currentTrack)->pcmData.size() / channels;
    }

    int32_t getCurrentTrackPosInFrames() {
        return currentFrame;
    }

    void setCurrentTrackPosInFrames(int32_t frame) {
        mtx.lock();

        currentFrame = frame;

        mtx.unlock();
    }

    void setTrack(SourceIt id) {
        PLOG_INFO << "Playback_state: setting track with id " << id->get();

        mtx.lock();

        currentTrack = id;
        currentFrame = 0;

        mtx.unlock();
    }

    void setPlaying(bool playing) {
        PLOG_INFO << "Playback_state: set playing state to " << playing;
        mtx.lock();

        isPlaying = playing;

        mtx.unlock();
    }

};

class Editor {
public:
    ma_device audio_device;

    MediaPool media_pool;
    PlaybackState playback_state;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        PlaybackState *playback_state = reinterpret_cast<PlaybackState*>(pDevice->pUserData);
        playback_state->getFrames(pOutput, frameCount);

        return;
    }

    Editor(): media_pool(), playback_state(media_pool) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.dataCallback = &Editor::data_callback;
        config.pUserData = &playback_state;

        if (ma_device_init(NULL, &config, &audio_device) != MA_SUCCESS) {
            PLOG_FATAL << "Failed to initialize audio engine";
            throw std::runtime_error("Audio init error");
        }

        ma_device_start(&audio_device);
        PLOG_INFO << "Editor class initialized";
    }

    ~Editor() {
        ma_device_stop(&audio_device);
        ma_device_uninit(&audio_device);
        PLOG_INFO << "Unitialized ma_device";
    }
};

void handle_media_pool_player(Editor& editor);

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

    Editor editor;

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

        PLOG_VERBOSE<< "Calling imgui update";
        ImGui::SFML::Update(window, deltaClock.restart());
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);


        ImGui::ShowDemoWindow();


        // MAIN WINDOW
        // ImGui::SetNextWindowSize({(float)window.getSize().x, (float)window.getSize().y});
        ImGui::Begin("Audio editor", NULL,  /*ImGuiWindowFlags_NoNav |
                                            ImGuiWindowFlags_NoDecoration |
                                            ImGuiWindowFlags_NoInputs*/ 0);


        ImGui::End(); // MAIN WINDOW

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
                    AudioSourcePtr src = decode_audio_from_file(name, path);

                    editor.media_pool.push_back(src);
                }

            }

            // close
            ImGuiFileDialog::Instance()->Close();
        }

        handle_media_pool_player(editor);

        ImGui::End(); // media pool

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


void handle_media_pool_player(Editor& editor) {
    int track_idx = 0;
    SourceIt eraseIt = editor.media_pool.end();

    for (auto it = editor.media_pool.begin(); it != editor.media_pool.end(); it++, track_idx++) {

        MediaPool &pool = editor.media_pool;
        const AudioSourcePtr src = *it;

        bool playing = editor.playback_state.isPlaying;
        SourceIt currentTrack = editor.playback_state.currentTrack;
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
