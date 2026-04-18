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

// Db to linear conversion
inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Simple clamping 
inline float clampSample(float sample, float threshold = 0.99f) {
    if (sample > threshold) return threshold;
    if (sample < -threshold) return -threshold;
    return sample;
}

static const size_t START_RENDER_BUFFER_SIZE = 4096; 

struct AudioSource {
    bool valid = false;
    std::string name;
    std::string path;

    std::atomic<bool> loading = false; // use when loading asynchronously 
    
    std::vector<float> pcmData;

    AudioSource(const std::string& name_, const std::string& path_): name(name_), path(path_) {}
     
    float getMonoSampleAmplitude(ma_uint64 frame) {
        if (frame >= pcmData.size() / INNER_CHANNELS) return 0;

        float avg_amp = 0;
        for (int i = 0; i < INNER_CHANNELS; i++) {
            avg_amp += pcmData[frame*INNER_CHANNELS + i];
        }

        avg_amp /= INNER_CHANNELS;
        return avg_amp;
    }

    size_t getDuration() const {
        return pcmData.size() / INNER_CHANNELS;
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
    float gain_db = 0;        // громкость в децибелах (или линейный множитель)
    float pan = 0;            // панорама: -1.0 (лево) ... 0.0 (центр) ... 1.0 (право)
    bool muted = false;           // быстрый мьют без удаления

    // === Fade in/out ===
    std::optional<std::pair<float, float>> fade_in;  // {duration_sec, curve}
    std::optional<std::pair<float, float>> fade_out;

    // === Helpers ===
    ma_uint64 getSourceDuration() const {
        if (source && source->valid)
            return source->getDuration();

        return 0;
    }

    ma_int64 getDurationFrames() const {
        return source_end_frame - source_start_frame;
    }

    ma_int64 getTimelineEndFrame() const {
        return timeline_start_frame + getDurationFrames();
    }

    // Конвертация: время на таймлайне -> кадр в источнике
    std::optional<ma_uint64> timelineToSourceFrame(ma_int64 timeline_frame) const {
        if (timeline_frame < timeline_start_frame ||
            timeline_frame >= getTimelineEndFrame()) {
            return std::nullopt; // кадр вне границ клипа
        }
        ma_uint64 clip_local_frame = timeline_frame - timeline_start_frame;
        return source_start_frame + clip_local_frame;
    }

    /// Renders frames to out array, ADDITIVELY 
    /// Doesn't write zeros
    void renderFrames(std::vector<audio_sample_t> &out, ma_int64 start_frame, ma_uint64 frame_count);

    /// Trim clip from left (false) or right(true) to position timeline_pos
    /// @return Trim made any changes
    bool trim(bool right, ma_int64 timeline_pos);
    /// Cut clip on timeline_pos. On success returns new created clip
    std::optional<Clip> cut(ma_int64 timeline_pos);

    friend std::ostream& operator<<(std::ostream& os, const Clip& clip);

    Clip copy() {
        Clip new_clip = *this;
        new_clip.setUniqueId();
        return new_clip;
    }

    Clip(AudioSourcePtr src, ma_int64 timeline_pos):
        source(src), timeline_start_frame(timeline_pos),
        source_start_frame(0), source_end_frame(src->pcmData.size() / INNER_CHANNELS)
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
    FreqDomainEffect fft_pipeline;
    Equalizer equalizer;
    // ================ Methods ================================

    const std::vector<audio_sample_t> &renderBlock(ma_uint64 start_frame);
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
        rendering_buffer(mtx_, START_RENDER_BUFFER_SIZE*INNER_CHANNELS),
        fft_pipeline(render_block_size, INNER_CHANNELS),
        equalizer(render_block_size) 
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

    TimeLine(std::mutex &mtx_): 
        mtx(mtx_), 
        rendering_buffer(render_buffer_mtx, START_RENDER_BUFFER_SIZE * INNER_CHANNELS),
        block_adapter(render_block_size * INNER_CHANNELS * 2) {}

    float gain_db = 0; // master gain

    // === Methods ===
    size_t getTrackCount() { return tracks.size(); }

    Track &getTrack(size_t idx) {
        auto elem = tracks.begin();
        std::advance(elem, idx);
        return *elem;
    }

    Track *getTrackById(TrackId_t id);

    const std::vector<audio_sample_t>& renderBlock(ma_uint64 start_frame);
    
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
