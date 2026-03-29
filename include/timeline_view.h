#pragma once

#include "timeline.h"

#include "imgui.h"

namespace waves {

struct TimelineInteraction {
    enum class Mode { None, Selecting, DraggingClip, ResizingClip } mode;
    ClipId_t active_clip_id;
    ma_uint64 drag_start_frame; // позиция клипа в момент начала перетаскивания, needed for undo/redo
    ImVec2 mouse_start_pos;

    TimelineInteraction(): mode(Mode::None) {}

};

class TimelineView {
    ma_uint64 total_frames;      // общая длина проекта

    float pixels_per_frame;      // масштаб: сколько пикселей на один кадр
    const float MAX_PPF = 100.f;
    const float MIN_PPF = 0.0001f;  //

    ma_uint64 scroll_frame;      // кадр, соответствующий левому краю видимой области

    ImU32 grid_line_col = IM_COL32(0x55, 0x6b, 0xa3, 255);
    float beats_per_second = 2.f;
    int   time_signature = 4;

    float track_height = 120.f;
    const float MIN_TRACK_HEIGHT = 50.f;
    const float MAX_TRACK_HEIGHT = 500.f;

    const int MAX_POINTS_PER_WAVEFORM = 10000;

    // drawing state

    ImVec2 canvas_pos;
    float  canvas_width;
    ImVec2 full_canvas_size;

    TimelineInteraction interaction;

public:

    TimelineView(ma_uint64 len, float scale):
        total_frames(len), pixels_per_frame(scale), scroll_frame(0)
    {}

    // Кадр -> позиция в пикселях (относительно левого края канваса)
    float frameToPixel(ma_uint64 frame) const {
        return static_cast<float>(frame - scroll_frame) * pixels_per_frame;
    }

    float frameToPixelRel(ma_uint64 frame) const {
        return static_cast<float>(frame) * pixels_per_frame;
    }

    // Пиксель -> кадр
    ma_uint64 pixelToFrame(float pixel_x) const {
        return scroll_frame + static_cast<ma_uint64>(pixel_x / pixels_per_frame);
    }

    ma_int64  pixelToFrameRel(float pixel_x) const {
        return static_cast<ma_uint64>(pixel_x / pixels_per_frame);
    }

    // convert frame to time in milliseconds
    float frameToMillis(ma_uint64 frame) const {
        const float MILLIS_PER_SEC = 1000;
        return static_cast<float>(frame) / INNER_SAMPLE_RATE * MILLIS_PER_SEC;
    }

    std::pair<ma_uint64, ma_uint64> getVisibleFramesRange() const {
        return {pixelToFrame(0), pixelToFrame(canvas_width)};
    }

    std::pair<ma_uint64, float> getNearestBeatInPixels() const {
        // |  scroll  |       |
        const ma_uint64 step = getBeatStepInFrames();
        ma_uint64 beat_idx = (scroll_frame + step - 1) / step;
        return {beat_idx, frameToPixel(beat_idx*step)};
    }

    ImU32 getGridLineCol() const {
        return grid_line_col;
    }

    ma_uint64 getBeatStepInFrames() const {
        return beats_per_second * INNER_SAMPLE_RATE;
    }

    float getBeatStepInPixels() const {
        return getBeatStepInFrames() * pixels_per_frame;
    }


    std::pair<ma_uint64, ma_uint64> getVisibleClipRange(const Clip& clip) const {
        auto [vis_left, vis_right] = getVisibleFramesRange();
        ma_uint64 left = std::max(clip.timeline_start_frame, vis_left);
        ma_uint64 right = std::min(vis_right, clip.getTimelineEndFrame());

        return {left, right};
    }

    // === УТИЛИТЫ ДЛЯ ЗУМА И СКРОЛЛА ===

    void zoomAtPixel(float pixel_x, float zoom_factor) {
        float new_ppf = pixels_per_frame * zoom_factor;
        if (new_ppf > MAX_PPF || new_ppf < MIN_PPF) {
            return;
        }

        pixels_per_frame = new_ppf;
        // Увеличиваем масштаб, сохраняя позицию под курсором
        ma_uint64 frame_under_cursor = pixelToFrame(pixel_x);
        // Корректируем скролл, чтобы кадр под курсором остался на месте
        scroll_frame = frame_under_cursor - static_cast<ma_uint64>(pixel_x / pixels_per_frame);
    }

    void scrollByFrames(int64_t delta_frames) {
        PLOG_DEBUG << "Scrolling timeline by " << delta_frames << " frames";
        if (delta_frames > 0) {
            scroll_frame = std::min(scroll_frame + delta_frames, total_frames);
        } else {
            scroll_frame = (scroll_frame > static_cast<ma_uint64>(-delta_frames))
                ? scroll_frame + delta_frames : 0;
        }
    }

    // ====

    bool HandleClipInteraction(const Clip& clip,
                           ImVec2 canvas_pos, ImVec2 mouse_pos,
                           bool& out_clicked, bool& out_hovered);

    bool HandleClipDrag(Clip& clip, ImVec2 mouse_delta);

    // ======== DRAWING ==============
    void DrawMiniWaveform(ImDrawList* draw_list, const Clip& clip,
                      ImVec2 canvas_pos, float height, std::pair<ma_uint64, ma_uint64> clip_timeline_frames);

    void DrawTimeGrid(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size);


    void DrawClip(ImDrawList* draw_list, const Clip& clip,
                ImVec2 canvas_pos, bool is_selected, bool is_hovered);

    void DrawTrack(Track& track);

    void DrawTimeline(TimeLine& timeline);

};




} // namespace waves
