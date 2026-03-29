#pragma once

#include "common.h"

#include "miniaudio.h"
#include <mutex>
#include <stdexcept>

#include "miniaudio_utils.h"
#include "timeline.h"
#include "timeline_view.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path);

using MediaPool = std::list<AudioSourcePtr>;
using SourceIt = typeof(MediaPool().begin());

struct PlaybackState {
    bool isPlaying = false;

    int64_t currentFrame = 0;
    SourceIt currentTrack;

    MediaPool& pool;

    std::mutex mtx;

    PlaybackState(MediaPool& pool_) : pool(pool_) {}

    void getFrames(void *out, ma_uint32 frameCount) {
        mtx.lock();
        if (isPlaying) {
            PLOG_VERBOSE << "getFrames callback: writing " << frameCount << " frames to " << out;

            const std::vector<float>& pcmData = (*currentTrack)->pcmData;
            const size_t trackLen = pcmData.size();

            auto startIt = (INNER_CHANNELS*currentFrame >= trackLen ) ?
                            pcmData.end() :
                            pcmData.begin() + INNER_CHANNELS*currentFrame;
            auto endIt = (INNER_CHANNELS*(currentFrame + frameCount) >= trackLen) ?
                            pcmData.end() :
                            pcmData.begin() + INNER_CHANNELS*(currentFrame + frameCount);

            std::copy(startIt, endIt, reinterpret_cast<float*>(out));
            currentFrame += frameCount;
        }

        mtx.unlock();
    }

    int32_t getCurrentTrackLenInFrames() {
        return (*currentTrack)->pcmData.size() / INNER_CHANNELS;
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

    MaAudioPlayer player;
    MediaPool media_pool;
    PlaybackState playback_state;

    TimelineView tl_view{static_cast<ma_uint64>(1e6), 1e-2};
    TimeLine timeline;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        PlaybackState *playback_state = reinterpret_cast<PlaybackState*>(pDevice->pUserData);
        playback_state->getFrames(pOutput, frameCount);

        return;
    }

    Editor():
        player(ma_format_f32, INNER_CHANNELS, INNER_SAMPLE_RATE, &Editor::data_callback, &playback_state),
        media_pool(), playback_state(media_pool)
    {
        timeline.tracks.push_back(Track());
        PLOG_INFO << "Editor class initialized";
    }

    ~Editor() {

    }
};

} // namespace waves
