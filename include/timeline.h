#pragma once
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>
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

    // Clip(const Clip& other) = default;
    // Clip(Clip&& other) noexcept = default;

    // Clip& operator=(const Clip& other) = default;
    // Clip(const Clip& other): source(other.source), name(other.name), timeline_start_frame(other.timeline_start_frame),
    //     source_start_frame(other.source_start_frame), source_end_frame(other.source_end_frame) {
    //     id = unique_id_++;
    // }
};

inline ClipId_t Clip::unique_id_ = 0;

// Use case: 
// Audio thread -> renders pack of frames and then uses them on next stage
// GUI thread   -> gets latest pack of rendered frames for visualization
// Overall data flow is controlled by audio thread
struct ReadableStreamingBuffer {
private:
    std::vector<audio_sample_t> buffers[3];
    std::int16_t latest_buffer = 0;
    std::int16_t reader_buffer = 0; 
    std::int16_t writer_buffer = -1; 

    std::mutex& mtx;

public:
    ReadableStreamingBuffer(std::mutex& mtx_, size_t elems);
    /* =============== WRITER THREAD ========================== */

    // Get free buffer filled with zeros
    std::vector<audio_sample_t> &writerGetBuffer(size_t elems);
    // Use after writerGetBuffer to mark it as ready 
    // It is guaranteed that this buffer will be read only at least until next getBuffer() call
    // Also this buffer won't be rewritten if reader took it
    const std::vector<audio_sample_t> &writerSentReadyBuffer();

    /* ============== READER THREAD =========================== */

    // Get latest buffer ready for processing
    // Also releases previously tooken buffer
    const std::vector<audio_sample_t> &readerGetReadyBuffer();

};

// // Data structure to seamlessy use double bufferization in audio rendering 
// struct DoubleBuffer {
// private:
//     std::vector<audio_sample_t> buffers[2];
//     // std::shared_mutex smtx;

//     uint32_t current_idx = 0; 
//     //! DANGEROUS
//     std::vector<audio_sample_t> *finished;
//     // current_idx     -> current
//     // 1 - current_idx -> finished
// public:

//     DoubleBuffer(size_t size) {
//         buffers[0].resize(size);
//         buffers[1].resize(size);
//         finished = &buffers[1];
//     }

//     std::vector<audio_sample_t> &getCurrent() { return buffers[current_idx]; }
//     std::vector<audio_sample_t> &getFinished() { return buffers[1-current_idx]; }

//     std::vector<audio_sample_t> &swapBufs() {
//         current_idx = 1 - current_idx;
//         finished = &getFinished();
//         return getFinished();
//     }
//     audio_sample_t  operator[](size_t idx) const { return buffers[current_idx][idx]; } 
//     audio_sample_t &operator[](size_t idx)       { return buffers[current_idx][idx]; }
//     // Resize buffers
//     void resize(size_t size) { 
//         buffers[0].resize(size);
//         buffers[1].resize(size);
//     }
//     // Prepare current buffer for rendering by filling it with zeros
//     // May perform resizing
//     void prepare(size_t elems) {
//         auto &buf = getCurrent();
//         //! Taking into account that vector never reallocates down 
//         buf.resize(elems);
//         std::fill(buf.begin(), buf.begin() + elems, 0);
//     }
// };

class Track {
public:
    std::string name;
    std::vector<Clip> clips;

    ReadableStreamingBuffer rendering_buffer;

    float gain_db = 0;
    float pan = 0;
    bool  mute = false;

    // ================ Methods ================================

    const std::vector<audio_sample_t> &renderFrames(ma_uint64 start_frame, ma_uint64 frame_count);

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

    ReadableStreamingBuffer rendering_buffer;

    TimelineClipboard clipboard; 

    std::mutex &mtx; // shared mtx

    std::mutex render_buffer_mtx;

    TimeLine(std::mutex &mtx_): mtx(mtx_), rendering_buffer(render_buffer_mtx, START_RENDER_BUFFER_SIZE * INNER_CHANNELS) {}

    float gain_db = 0; // master gain

    // === Methods ===
    size_t getTrackCount() { return tracks.size(); }
    
    Track &getTrack(size_t idx) {
        auto elem = tracks.begin();
        std::advance(elem, idx);
        return *elem;
    }

    const std::vector<audio_sample_t>& renderFrames(ma_uint64 start_frame, ma_uint64 frame_count);

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
