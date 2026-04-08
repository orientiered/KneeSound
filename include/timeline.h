#pragma once
#include <atomic>
#include <cmath>
#include "common.h"

#include "miniaudio.h"

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


struct AudioSource {
    bool valid = false;
    std::string name;
    std::string path;
    std::vector<float> pcmData;

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

    Clip(AudioSourcePtr src, ma_uint64 timeline_pos, std::optional<std::string> clip_name = std::nullopt):
        source(src), name(clip_name ? *clip_name : src->name), timeline_start_frame(timeline_pos),
        source_start_frame(0), source_end_frame(src->pcmData.size() / INNER_CHANNELS)
    {
        id = unique_id_++; // setting unique id on construction

    }

    // Clip(const Clip& other) = default;
    // Clip(Clip&& other) noexcept = default;

    // Clip& operator=(const Clip& other) = default;
    // Clip(const Clip& other): source(other.source), name(other.name), timeline_start_frame(other.timeline_start_frame),
    //     source_start_frame(other.source_start_frame), source_end_frame(other.source_end_frame) {
    //     id = unique_id_++;
    // }
};

inline ClipId_t Clip::unique_id_ = 0;

class Track {
public:
    std::string name;
    std::vector<Clip> clips;


    std::vector<audio_sample_t> rendering_buffer;

    float gain_db = 0;
    float pan = 0;
    bool  mute = false;

    // ================ Methods ================================

    std::vector<audio_sample_t> &renderFrames(ma_uint64 start_frame, ma_uint64 frame_count);

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

    Track() : name("None"), rendering_buffer(4096*INNER_CHANNELS) {}
};

struct ClipLoc {
    size_t track_idx;
    size_t clip_idx;
};

class TimeLine {
public:
    std::vector<Track> tracks;

    std::atomic<ma_uint64> playhead_frame;

    std::vector<audio_sample_t> rendering_buffer;

    float gain_db = 0; // master gain
    // === Methods ===

    std::vector<audio_sample_t>& renderFrames(ma_uint64 start_frame, ma_uint64 frame_count);

    bool isValidClipId(ClipId_t id);

    std::optional<ClipLoc> getTrackAndClipIdx(ClipId_t id);
    Clip *getClipById(ClipId_t id);
    std::optional<size_t>  getTrackIdx(ClipId_t id);
    void removeClipById(ClipId_t id);

    void moveClipToTrack(ClipId_t id, int track_idx);

    ClipId_t addClip(const Clip& clip, int track_idx);
    // std::vector<audio_sample_t>



};

} // namespace waves
