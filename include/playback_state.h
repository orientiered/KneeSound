#pragma once
#include "common.h"

#include "mutex"
#include "timeline.h"

namespace waves {

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
            const int64_t trackLenInFrames = trackLen / INNER_CHANNELS;

            auto startIt = (INNER_CHANNELS*currentFrame >= trackLen ) ?
                            pcmData.end() :
                            pcmData.begin() + INNER_CHANNELS*currentFrame;
            auto endIt = (INNER_CHANNELS*(currentFrame + frameCount) >= trackLen) ?
                            pcmData.end() :
                            pcmData.begin() + INNER_CHANNELS*(currentFrame + frameCount);

            std::copy(startIt, endIt, reinterpret_cast<float*>(out));
            
            currentFrame = std::min(trackLenInFrames, currentFrame +frameCount);
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

}