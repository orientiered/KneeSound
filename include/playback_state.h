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

struct PlaybackState {
    bool isPlaying = false;
    SampleSource src = POOL_SRC;

    int64_t currentFrame = 0;
    SourceIt currentTrack;

    MediaPool& pool;
    TimeLine& timeline;

    std::mutex mtx;

    PlaybackState(MediaPool& pool_, TimeLine& timeline_) : pool(pool_), timeline(timeline_) {}

    void getFrames(void *out, ma_uint32 frameCount) {
        mtx.lock();
        
        if (src == POOL_SRC) {
            getFramesFromPool(out, frameCount);
        } else {
            getFramesFromTimeline(out, frameCount);
        }

        mtx.unlock();
    }

    void getFramesFromTimeline(void *out, ma_uint32 frameCount) {
        if (isPlaying) {
            PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << 
                "timeline callback: writing " << frameCount << " frames to " << out;

            auto &buffer = timeline.renderFrames(timeline.playhead_frame, frameCount);
            std::copy(buffer.begin(), buffer.begin() + frameCount * INNER_CHANNELS, reinterpret_cast<float*>(out));

            timeline.playhead_frame.fetch_add(frameCount);
        }
        
    }

    void getFramesFromPool(void* out, ma_uint32 frameCount) { 
        if (isPlaying) {
            PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << 
                "pool callback: writing " << frameCount << " frames to " << out;

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
    }

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