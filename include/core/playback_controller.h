#pragma once
#include "common.h"

#include "core/audio_source.h"
#include "timeline.h"

#include "core/miniaudio_utils.h"
#include <atomic>
#include <memory>

namespace waves {

class MediaPool {
    std::vector<AudioSourcePtr> audio_src_;

    std::atomic<AudioSourcePtr> current_track_;
    std::atomic<int64_t> current_frame_;

    std::atomic<bool> is_playing_;
public:
    const std::vector<AudioSourcePtr> &getTrackList() const {
        return audio_src_;
    }

    void push_back(AudioSourcePtr src) {
        audio_src_.push_back(src);
    }

    void clear() {
        resetTrack();
        audio_src_.clear();
    }
    
    void erase(AudioSourcePtr src) {
        auto it = std::find(audio_src_.begin(), audio_src_.end(), src);

        if (src == current_track_.load()) {
            resetTrack();
        }

        audio_src_.erase(it);
    }

    int32_t getCurrentTrackLenInFrames() const {
        AudioSourcePtr src = current_track_.load();
        if (src) return src->getDurationFrames();
        return 0;
    }

    int32_t getCurrentFrame() const { return current_frame_.load(); }

    AudioSourcePtr getCurrentTrack() const { return current_track_.load(); }

    void setCurrentTrackPosInFrames(int64_t frame) {
        current_frame_.store(frame);
    }

    void setTrack(AudioSourcePtr src) {
        PLOG_INFO << "Media pool: setting track " << (src ? src->name : "empty");

        current_track_.store(src, std::memory_order_release);
        current_frame_.store(0, std::memory_order_release);
    }

    void resetTrack() { setTrack(nullptr); }

    bool getPlaying() const { return is_playing_.load(); }
    void setPlaying(bool state) { is_playing_.store(state, std::memory_order_release); }
};

// Source of samples:
// POOL - media pool with original audio
// TIMELINE - render timeline
enum SampleSource {
    POOL_SRC,
    TIMELINE_SRC
};

class PlaybackController {
    std::atomic<bool> timeline_playing_ = false;
    std::atomic<SampleSource> src = POOL_SRC;

public:
    MediaPool& pool;
    TimeLine& timeline;

    MaAudioPlayer player;

    PlaybackController(MediaPool& pool_, TimeLine& timeline_) :
        pool(pool_), timeline(timeline_),
        player(ma_format_f32, INNER_CHANNELS, INNER_SAMPLE_RATE, &data_callback, this)
        {}

    void getFrames(void *out, ma_uint32 frameCount) {

        if (src.load() == POOL_SRC) {
            getFramesFromPool(out, frameCount);
        } else {
            getFramesFromTimeline(out, frameCount);
        }

    }

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        PlaybackController *playback_state = reinterpret_cast<PlaybackController*>(pDevice->pUserData);
        playback_state->getFrames(pOutput, frameCount);

        return;
    }

    void getFramesFromTimeline(void *out, ma_uint32 frameCount);

    void getFramesFromPool(void* out, ma_uint32 frameCount);

    void handleToggleFromTimeline() {
        PLOG_DEBUG << "Playback toggle from timeline";

        timeline_playing_ = !timeline_playing_;
        pool.setPlaying(false);
        src = TIMELINE_SRC;
    }

    void setPoolSrc() {
        PLOG_DEBUG << "Setting pool src";

        timeline_playing_ = false;
        src = POOL_SRC;
    }

    bool getPlaying() const { return timeline_playing_.load(); }
    void setPlaying(bool state) { timeline_playing_.store(state, std::memory_order_release); }

};

}
