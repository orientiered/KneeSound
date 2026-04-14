#pragma once
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include "common.h"

#include "kiss_fftr.h"
#include "miniaudio.h"

#include "buffer_utils.h"

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
};

using AudioSourcePtr = std::shared_ptr<AudioSource>;

using ClipId_t = int64_t;
const ClipId_t CLIP_NONE = -1;

struct Clip {
private:
    static ClipId_t unique_id_;
public:
    // ==== Data ===
    ClipId_t id; // used for interaction handling
    std::string name; // UI name
    AudioSourcePtr source;

    // === Boundaries  ===
    ma_uint64 source_start_frame;   // inclusive
    ma_uint64 source_end_frame;     // exclusive
    // [source_start_frame, source_end_frame)
    ma_uint64 timeline_start_frame;

    // === АУДИО-ПАРАМЕТРЫ ===
    float gain_db = 0;        // громкость в децибелах (или линейный множитель)
    float pan = 0;            // панорама: -1.0 (лево) ... 0.0 (центр) ... 1.0 (право)
    bool muted = false;           // быстрый мьют без удаления

    // === ВИЗУАЛИЗАЦИЯ (для UI) ===
    uint32_t color;       // цвет клипа в таймлайне
    std::optional<std::pair<float, float>> fade_in;  // {duration_sec, curve}
    std::optional<std::pair<float, float>> fade_out;

    // === ОБРАБОТКА (эффекты и кэширование) ===
    // std::vector<std::unique_ptr<AudioEffect>> effects; // цепочка эффектов
    // std::vector<float> pre_rendered_buffer; // кэш после обработки
    // bool pre_render_valid; // флаг валидности кэша

    // === ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ===
    ma_uint64 getDurationFrames() const {
        return source_end_frame - source_start_frame;
    }

    ma_uint64 getTimelineEndFrame() const {
        return timeline_start_frame + getDurationFrames();
    }

    // Конвертация: время на таймлайне -> кадр в источнике
    std::optional<ma_uint64> timelineToSourceFrame(ma_uint64 timeline_frame) const {
        if (timeline_frame < timeline_start_frame ||
            timeline_frame >= getTimelineEndFrame()) {
            return std::nullopt; // кадр вне границ клипа
        }
        ma_uint64 clip_local_frame = timeline_frame - timeline_start_frame;
        return source_start_frame + clip_local_frame;
    }

    // Renders frames to out array, ADDITIVELY 
    // Doesn't write zeros
    void renderFrames(std::vector<audio_sample_t> &out, ma_uint64 start_frame, ma_uint64 frame_count);

    std::optional<Clip> cut(ma_uint64 timeline_pos);

    friend std::ostream& operator<<(std::ostream& os, const Clip& clip);

    Clip copy() {
        Clip new_clip = *this;
        new_clip.id = unique_id_++;
        return new_clip;
    }

    Clip(AudioSourcePtr src, ma_uint64 timeline_pos, std::optional<std::string> clip_name = std::nullopt):
        source(src), name(clip_name ? *clip_name : src->name), timeline_start_frame(timeline_pos),
        source_start_frame(0), source_end_frame(src->pcmData.size() / INNER_CHANNELS)
    {
        id = unique_id_++; // setting unique id on construction

    }

};

inline ClipId_t Clip::unique_id_ = 0;


/* ========================== EQUALIZER ======================== */
class Equalizer {
private:
    kiss_fftr_cfg forward_cfg;
    kiss_fftr_cfg inverse_cfg;

public:

    
};

/* ========================== TRACK ============================ */
class Track {
public:
    std::string name;
    std::vector<Clip> clips;

    ReadableStreamingBuffer rendering_buffer;

    const size_t render_block_size = RENDER_BLOCK_SIZE;
    
    float gain_db = 0;
    float pan = 0;
    bool  mute = false;

    // ================ Methods ================================

    const std::vector<audio_sample_t> &renderBlock(ma_uint64 start_frame);

    void addClip(Clip&& clip) {
        PLOG_INFO << "Add clip '" << clip.name << "' [" << &clip << "] to track '" << name << "'";
        PLOG_INFO << "Clip len " << clip.getDurationFrames() << " frames";
        clips.push_back(std::move(clip));
    }

    void addClip(const Clip& clip) {
        PLOG_INFO << "Add clip '" << clip.name << "' [" << &clip << "] to track '" << name << "'";
        PLOG_INFO << "Clip len " << clip.getDurationFrames() << " frames";
        clips.push_back(clip);
    }

    Track(std::mutex& mtx_) : name("None"), rendering_buffer(mtx_, START_RENDER_BUFFER_SIZE*INNER_CHANNELS) {}
};

struct ClipLoc {
    size_t track_idx;
    size_t clip_idx;
};

struct TimelineClipboard {
    std::optional<Clip> data;


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

    TimelineClipboard clipboard; 

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

    const std::vector<audio_sample_t>& renderBlock(ma_uint64 start_frame);
    
    void renderFrames(audio_sample_t *out, ma_uint64 start_frame, ma_uint64 frame_count);

    bool isValidClipId(ClipId_t id);

    std::optional<ClipLoc> getTrackAndClipIdx(ClipId_t id);
    Clip *getClipById(ClipId_t id);
    std::optional<size_t>  getTrackIdx(ClipId_t id);

    // Methods that destroy clips require mtx for synchronization
    void removeClipByLoc(ClipLoc loc); // uses mtx
    void removeClipById(ClipId_t id); // uses mtx

    void moveClipToTrack(ClipId_t id, int track_idx); // uses mtx

    void addTrack();
    ClipId_t addClip(const Clip& clip, int track_idx);

    // clipboard
    void copyToClipboard(ClipId_t id);
    void cutToClipboard(ClipId_t id);  // uses mtx

    std::optional<Clip> pasteFromClipboard();

};

} // namespace waves
