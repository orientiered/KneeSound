#pragma once

#include "common.h"

#include <mutex>
#include "miniaudio.h"

#include "miniaudio_utils.h"
#include "timeline.h"
#include "timeline_view.h"

#include "playback_state.h"

#include "media_pool_view.h"
#include "wav_exporter.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path);

class Editor {
public:

    std::mutex mtx;


    MediaPool media_pool;
    TimeLine timeline;
    PlaybackState playback_state;

    MediaPoolView mp_view;
    TimelineView tl_view{static_cast<ma_uint64>(1e6), 1e-2};

    Exporter_View exporter;
    
    MaAudioPlayer player;

    bool show_export_window = false;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        PlaybackState *playback_state = reinterpret_cast<PlaybackState*>(pDevice->pUserData);
        playback_state->getFrames(pOutput, frameCount);

        return;
    }

    Editor(): mtx(), 
        media_pool(), 
        timeline(mtx),
        playback_state(mtx, media_pool, timeline),
        player(ma_format_f32, INNER_CHANNELS, INNER_SAMPLE_RATE, &Editor::data_callback, &playback_state)
    {
        timeline.tracks.push_back(Track());
        PLOG_INFO << "Editor class initialized";
    }

    void Draw();
    void DrawExport();

    ~Editor() {

    }
};

} // namespace waves
