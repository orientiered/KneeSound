#pragma once

#include "common.h"

#include <mutex>
#include "miniaudio.h"

#include "miniaudio_utils.h"
#include "timeline.h"
#include "timeline_view.h"

#include "playback_controller.h"

#include "media_pool_view.h"
#include "wav_exporter.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path);
AudioSourcePtr decode_audio_from_file_async(const std::string& name, const std::string& path);

class Editor {
public:

    std::mutex mtx;


    MediaPool media_pool;
    TimeLine timeline;
    PlaybackController playback_state;

    MediaPoolView mp_view;
    TimelineView tl_view;

    Exporter_View exporter;
    
    MaAudioPlayer player;

    bool show_export_window = false;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        PlaybackController *playback_state = reinterpret_cast<PlaybackController*>(pDevice->pUserData);
        playback_state->getFrames(pOutput, frameCount);

        return;
    }

    Editor(): mtx(), 
        media_pool(), 
        timeline(mtx),
        playback_state(mtx, media_pool, timeline),
        tl_view(timeline, static_cast<ma_uint64>(1e6), 1e-2),
        player(ma_format_f32, INNER_CHANNELS, INNER_SAMPLE_RATE, &Editor::data_callback, &playback_state)
    {
        timeline.addTrack();
        // timeline.tracks.resize(1);
        // timeline.tracks.emplace_back();
        PLOG_INFO << "Editor class initialized";
    }

    void Draw();
    void DrawExport();

    ~Editor() {

    }
};

} // namespace waves
