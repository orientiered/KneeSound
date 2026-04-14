#pragma once
#include "common.h"

#include "mutex"
#include "timeline.h"

namespace waves {

using MediaPool = std::list<AudioSourcePtr>;
using SourceIt = typeof(MediaPool().begin());

// Source of samples:
// POOL - media pool with original audio
// TIMELINE - render timeline
enum SampleSource {
    POOL_SRC,
    TIMELINE_SRC
}; 

struct PlaybackController {
    bool isPlaying = false;
    SampleSource src = POOL_SRC;

    int64_t currentFrame = 0;
    SourceIt currentTrack;

    MediaPool& pool;
    TimeLine& timeline;

    std::mutex &mtx;
    

    PlaybackController(std::mutex &mtx_, MediaPool& pool_, TimeLine& timeline_) : 
        mtx(mtx_), pool(pool_), timeline(timeline_) {}

    void getFrames(void *out, ma_uint32 frameCount) {
        std::lock_guard<std::mutex> lock_guard(mtx);
        
        if (!isPlaying) return;

        if (src == POOL_SRC) {
            getFramesFromPool(out, frameCount);
        } else {
            getFramesFromTimeline(out, frameCount);
        }

    }

    void getFramesFromTimeline(void *out, ma_uint32 frameCount);

    void getFramesFromPool(void* out, ma_uint32 frameCount);

    void handleToggleFromTimeline() {
        PLOG_DEBUG << "Playback toggle from timeline";
        mtx.lock();

        if (src == POOL_SRC) {
            isPlaying = true;
        } else {
            isPlaying = !isPlaying;
        }

        src = TIMELINE_SRC;
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