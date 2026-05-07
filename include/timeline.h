#pragma once
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include "common.h"


#include "miniaudio.h"

#include "buffer_utils.h"
#include "audio_effects.h"

namespace waves {

// Simple clamping
inline float clampSample(float sample, float threshold = 0.99f) {
    if (sample > threshold) return threshold;
    if (sample < -threshold) return -threshold;
    return sample;
}

inline ma_int64 secToFrame(float sec) {
    return sec * INNER_SAMPLE_RATE;
}

inline float frameToSec(ma_int64 frame) {
    return static_cast<float>(frame) / INNER_SAMPLE_RATE;
}

inline std::pair<int, float> frameToMinSec(ma_int64 frame) {
    float total_sec = frameToSec(frame);
    int mins = total_sec / 60;
    float sec = total_sec - mins * 60;
    return {mins, sec};
}

inline audio_sample_t interleavedToMono(const audio_sample_t *sample, size_t channels) {
    float sum = 0;
    #pragma unroll
    for (int i = 0; i < channels; i++) {
        sum += sample[i];
    }

    return sum / channels;
}

static const size_t START_RENDER_BUFFER_SIZE = 4096;

struct PeakCache {
    ma_uint64 block_size_;
    struct min_max {
        float min = 10.0f;
        float max = -10.0f;
        void update(float s) {
            min = std::min(min, s);
            max = std::max(max, s);
        }
        void update(const min_max& other) {
            min = std::min(min, other.min);
            max = std::max(max, other.max);
        }
    };

    std::vector<min_max> peaks; // min and max in block

    PeakCache(ma_uint64 block_size, const AudioBuffer &samples);
    PeakCache(ma_uint64 block_size, const PeakCache &cache);
};

struct PeakCacheManager {
    std::vector<PeakCache> peak_caches;
    void build(const AudioBuffer &samples) {
        if (!peak_caches.empty()) return;

        peak_caches.emplace_back(16, samples);
        peak_caches.emplace_back(64, peak_caches.back());
        peak_caches.emplace_back(256, peak_caches.back());
        peak_caches.emplace_back(1024, peak_caches.back());
        peak_caches.emplace_back(4096, peak_caches.back());

        std::reverse(peak_caches.begin(), peak_caches.end());
    }

    std::optional<PeakCache::min_max> getPeak(ma_uint64 f_start, ma_uint64 f_end) const;
};

struct AudioSource {
    bool valid = false;
    std::string name;
    std::string path;

    std::atomic<bool> loading = false; // use when loading asynchronously

    MultiChannelBuffer pcmData;

    PeakCacheManager cache;

    AudioSource(const std::string& name_, const std::string& path_): name(name_), path(path_) {}

    float getMonoSampleAmplitude(ma_uint64 frame) const {
        return pcmData.getMeanSample(frame);
    }

    PeakCache::min_max getPeakFallback(ma_uint64 start, ma_uint64 end) const {
        PeakCache::min_max result;
        for (ma_uint64 f = start; f < end; ++f) {
            result.update(getMonoSampleAmplitude(f));
        }
        return result;
    }

    PeakCache::min_max getPeak(ma_uint64 start, ma_uint64 end) const {
        auto cached = cache.getPeak(start, end);
        if (cached) return *cached;

        return getPeakFallback(start, end);
    }


    ma_uint64 getDurationFrames() const {
        return pcmData.getFrameCount();
    }

};

using AudioSourcePtr = std::shared_ptr<AudioSource>;

using ClipId_t = int64_t;
const ClipId_t CLIP_NONE = -1;

struct Clip {
private:
    static ClipId_t unique_id_;
    ClipId_t setUniqueId() {
        id = unique_id_++;
        return id;
    }

public:
    // ==== Data ===
    ClipId_t id; // used for interaction handling
    AudioSourcePtr source;

    // === Boundaries  ===
    ma_int64 source_start_frame;   // inclusive
    ma_int64 source_end_frame;     // exclusive
    // [source_start_frame, source_end_frame)
    ma_int64 timeline_start_frame;

    // === General audio params ===
    double playback_speed = 1.0;
    static inline const double MAX_PLAYBACK_SPEED = 10.0;
    static inline const double MIN_PLAYBACK_SPEED = 0.1;

    float gain_db = 0;           // громкость в децибелах (или линейный множитель)
    float pan = 0;            // панорама: -1.0 (лево) ... 0.0 (центр) ... 1.0 (право)
    bool  muted = false;           // mute

    // === Fade in/out ===

    Fade fade_in{Fade::IN, 0};
    Fade fade_out{Fade::OUT, 0};

    // === Helpers ===
    ma_uint64 getSourceDuration() const {
        if (source && source->valid)
            return source->getDurationFrames();

        return 0;
    }

    ma_int64 getDurationFrames() const {
        return static_cast<double>(source_end_frame - source_start_frame) / playback_speed;
    }

    float getDurationSec() const {
        return frameToSec(getDurationFrames());
    }

    ma_int64 getTimelineEndFrame() const {
        return timeline_start_frame + getDurationFrames();
    }

    audio_sample_t getClipSrcFrame(uint32_t channel, ma_int64 src_frame) const {
        return source->pcmData[channel][src_frame];
    }

    double clipFrameToSrcFrame(ma_int64 clip_frame) const {
        return source_start_frame + static_cast<double>(clip_frame) * playback_speed;
    }

    ma_int64 srcFrameToClipFrame(double src_frame) const {
        return (src_frame - source_start_frame) / playback_speed;
    }

    audio_sample_t getClipFrameInterpolated(uint32_t channel, double src_frame) const;

    audio_sample_t getMonoClipFrame(ma_int64 clip_frame) const;

    PeakCache::min_max getPeak(ma_int64 clip_start_frame, ma_int64 clip_end_frame) const;

    // Конвертация: время на таймлайне -> кадр в источнике
    std::optional<ma_uint64> timelineToClipFrame(ma_int64 timeline_frame) const {
        if (timeline_frame < timeline_start_frame ||
            timeline_frame >= getTimelineEndFrame()) {
            return std::nullopt; // кадр вне границ клипа
        }
        ma_uint64 clip_local_frame = timeline_frame - timeline_start_frame;
        return clip_local_frame;
    }

    /// Renders frames to out buffer, ADDITIVELY
    /// Doesn't write zeros
    void renderFrames(AudioBuffer &out, ma_int64 start_frame, ma_uint64 frame_count);

    /// Trim clip from left (false) or right(true) to position timeline_pos
    /// @return Trim made any changes
    bool trim(bool right, ma_int64 timeline_pos);
    /// Cut clip on timeline_pos. On success returns new created clip
    std::optional<Clip> cut(ma_int64 timeline_pos);

    bool stretch(bool right, ma_int64 timeline_pos);

    double setPlaybackSpeed(double speed) {
        playback_speed = std::clamp(speed, MIN_PLAYBACK_SPEED, MAX_PLAYBACK_SPEED);
        return playback_speed;
    }

    friend std::ostream& operator<<(std::ostream& os, const Clip& clip);

    Clip copy() {
        Clip new_clip = *this;
        new_clip.setUniqueId();
        return new_clip;
    }

    Clip(AudioSourcePtr src, ma_int64 timeline_pos):
        source(src), timeline_start_frame(timeline_pos),
        source_start_frame(0), source_end_frame(src->pcmData.getFrameCount())
    {
        setUniqueId();
    }

};

inline ClipId_t Clip::unique_id_ = 0;

/* ========================== TRACK ============================ */
using TrackId_t = int64_t;
const TrackId_t TRACK_NONE = -1;

class Track {
private:
    static TrackId_t unique_id_;
    TrackId_t setUniqueId() {
        id = unique_id_++;
        return id;
    }
public:
    TrackId_t id;
    std::vector<Clip> clips;

    ReadableStreamingBuffer rendering_buffer;

    const size_t render_block_size = RENDER_BLOCK_SIZE;

    float gain_db = 0;
    float pan = 0;
    bool  mute = false;

    bool enable_eq = false;
    std::vector<EffectSlot> effects_;
    // PitchShifter  pitch;
    // ================ Methods ================================

    const AudioBuffer &renderBlock(ma_uint64 start_frame);
    size_t getLatency();

    void addClip(Clip&& clip) {
        PLOG_INFO << "Add clip '" << clip.id << "' [" << &clip << "] to track '" << id << "'";
        PLOG_INFO << "Clip len " << clip.getDurationFrames() << " frames";
        clips.push_back(std::move(clip));
    }

    void addClip(const Clip& clip) {
        PLOG_INFO << "Add clip '" << clip.id << "' [" << &clip << "] to track '" << id << "'";
        PLOG_INFO << "Clip len " << clip.getDurationFrames() << " frames";
        clips.push_back(clip);
    }

    Track(std::mutex& mtx_) :
        rendering_buffer(mtx_, START_RENDER_BUFFER_SIZE, INNER_CHANNELS)
    {
        setUniqueId();
    }
};

inline TrackId_t Track::unique_id_ = 0;

struct ClipLoc {
    size_t track_idx;
    size_t clip_idx;
};



/* MUTEX USAGE POLICY:

    Only GUI thread changes state of the timeline

    Audio thread only reads or modifies render buffers in Clips, Tracks and TimeLine, but never deletes them

    It means that only operation that delete clips or tracks must be synced with mutex

    Copying from gui thread is always safe
*/
class TimeLine {
    std::list<Track> tracks;
public:

    std::atomic<ma_uint64> playhead_frame;

    std::mutex render_buffer_mtx;
    ReadableStreamingBuffer rendering_buffer;

    std::mutex &mtx; // shared mtx

    const size_t render_block_size = RENDER_BLOCK_SIZE;

    AudioBlockAdapter block_adapter;
    std::vector<audio_sample_t> interleave_buffer;

    TimeLine(std::mutex &mtx_):
        mtx(mtx_),
        rendering_buffer(render_buffer_mtx, START_RENDER_BUFFER_SIZE, INNER_CHANNELS),
        block_adapter(render_block_size * INNER_CHANNELS * 2),
        interleave_buffer(render_block_size * INNER_CHANNELS) {}

    float gain_db = 0; // master gain

    // === Methods ===
    size_t getTrackCount() { return tracks.size(); }

    Track &getTrack(size_t idx) {
        auto elem = tracks.begin();
        std::advance(elem, idx);
        return *elem;
    }

    Track *getTrackById(TrackId_t id);

    const AudioBuffer& renderBlock(ma_uint64 start_frame);
    const std::vector<audio_sample_t> &renderBlockInterleaved(ma_uint64 start_frame);

    void renderFrames(audio_sample_t *out, ma_uint64 start_frame, ma_uint64 frame_count);

    bool isValidClipId(ClipId_t id);
    bool isValidTrackId(TrackId_t id);

    std::optional<ClipLoc> getTrackAndClipIdx(ClipId_t id);
    Clip *getClipById(ClipId_t id);
    std::optional<size_t>  getTrackIdx(ClipId_t id);

    // Methods that destroy clips require mtx for synchronization
    void removeClipByLoc(ClipLoc loc); // uses mtx
    void removeClipById(ClipId_t id); // uses mtx

    void moveClipToTrack(ClipId_t id, int track_idx); // uses mtx

    void addTrack();
    ClipId_t addClip(const Clip& clip, int track_idx);

};

} // namespace waves
