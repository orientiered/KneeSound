#pragma once

#include "common.h"

#include "miniaudio.h"

namespace waves {

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

struct Clip {
    // ==== Data ===
    std::string id; // ?remove
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
    bool solo = false;            // соло-режим

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

    Clip(AudioSourcePtr src, ma_uint64 timeline_pos, std::optional<std::string> clip_name = std::nullopt):
        source(src), name(clip_name ? *clip_name : src->name), timeline_start_frame(timeline_pos),
        source_start_frame(0), source_end_frame(src->pcmData.size() / INNER_CHANNELS)
    {

    }
};


class Track {
public:
    std::string name;
    std::vector<Clip> clips;

    float gain_db;
    float pan;
    bool  mute;

    void addClip(Clip&& clip) {
        PLOG_INFO << "Add clip '" << clip.name << "' to track '" << name << "'";
        PLOG_INFO << "Clip len " << clip.getDurationFrames() << " frames";
        clips.push_back(clip);
    }
};

class TimeLine {
public:
    std::vector<Track> tracks;

    ma_uint64 playing_head;
};

} // namespace waves
