#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include "common.h"

#include "miniaudio.h"

#include "core/timeline.h"
#include "core/playback_controller.h"

#include "effects/audio_effects.h"

#include "gui/timeline_view.h"
#include "gui/media_pool_view.h"


#include "wav_exporter.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path, bool async = true);

class Editor {
public:

    std::mutex mtx;

    PluginManager plugin_manager;

    MediaPool media_pool;
    TimeLine timeline;
    PlaybackController playback_state;

    MediaPoolView mp_view;
    TimelineView tl_view;

    Exporter_View exporter;

    Editor(): mtx(),
        media_pool(),
        timeline(mtx),
        playback_state(media_pool, timeline),
        tl_view(timeline, plugin_manager, 1e-2)
    {
        initPlugins();

        PLOG_INFO << "Initialized plugins";

        timeline.addTrack();

        PLOG_INFO << "Editor class initialized";
    }

    void Draw();
    ~Editor() {}
private:

    void initPlugins();
    void SaveProject(std::ofstream& output);
    void OpenProject(std::ifstream& input);

    void DrawExport();
    bool show_export_window = false;

    void DrawProjectImportExport();

    struct ProjectImportExport {
        std::filesystem::path knee_sound_project_path;
        enum Mode {
            None = 0,
            Import = 1,
            Export = 2
        } mode = None;
        bool show_file_choose = false;

        static const inline char *popup_key = "Error##ProjectErrorPopup";
        bool show_import_popup = false;
        std::string popup_msg = "";

        void StartOpen();
        void StartExport();
    } project_import_export_state;
};

} // namespace waves
