#pragma once

#include "audio_effects.h"
#include "common.h"

#include "imgui.h"
#include <imgui_internal.h>
#include <unordered_map>

#include "timeline.h"

namespace waves {

// Predefinition
class PlaybackController;

struct TimelineInteraction {
    enum class Mode { None, Selecting, DraggingClip, TrimmingClip, StretchingClip } mode;
    ClipId_t hovered_clip_id = CLIP_NONE; // Currently hovered clip
    ClipId_t selected_clip_id = CLIP_NONE;  // Selected clip id

    ClipId_t trimmed_clip_id = CLIP_NONE;
    bool trimming_right = false;

    ClipId_t stretched_clip_id = CLIP_NONE;
    bool stretching_right = false;

    ma_uint64 drag_start_frame; // позиция клипа в момент начала перетаскивания, needed for undo/redo
    ImVec2 mouse_start_pos;

    TimelineInteraction(): mode(Mode::None) {}

};

struct ClipView {
    std::string name = "Clip";

    // Color palette
    ImU32 col_clip_selected = IM_COL32(170, 190, 170, 220);
    ImU32 col_clip_base     = IM_COL32(150, 160, 150, 180);
    ImU32 col_clip_text     = IM_COL32(255, 255, 255, 255);

    // Waveform
    ImU32 col_waveform      = IM_COL32(255, 255, 255, 100);
    float gain_waveform     = 1.0f; ///< Amplification coefficient for waveform

};

struct TrackView {
    std::string name = "Track";

    ImU32 col_track_bg_odd  = IM_COL32(80, 80, 80, 200);
    ImU32 col_track_bg_even = IM_COL32(60, 60, 60, 200);
};

struct TimelineClipboard {
    std::optional<Clip> data;
    std::optional<ClipView> style;

};

struct ClipboardPayload {
    Clip clip;
    ClipView style;
};

class TimelineView {

    float pixels_per_frame;      // масштаб: сколько пикселей на один кадр
    const float MAX_PPF = 100.f;
    const float MIN_PPF = 0.0001f;  //

    ma_int64 scroll_frame;      // кадр, соответствующий левому краю видимой области

    ClipView clip_view_default{};
    ImU32 col_clip_fade = IM_COL32(140, 10, 10, 60);

    TrackView track_view_default{};

    ImU32 col_grid_line = IM_COL32(10, 10, 10, 255);
    ImU32 col_grid_line_text = IM_COL32(200, 200, 200, 255);
    float thickness_grid_line_major = 3.f;
    float thickness_grid_line_minor = 1.f;
    int   grid_line_count_limit = 10;
    int   grid_minor_lines_per_major = 4;

    float grid_line_header = 50.f;

    ImU32 col_playhead  = IM_COL32(209, 120, 5, 255);

    float beats_per_second = 2.f;
    int   time_signature = 4;

    float track_height = 120.f;
    float track_pad    = 2.f;
    float clip_vert_pad = 4.f;
    float track_info_width = 200.f;
    const float MIN_TRACK_HEIGHT = 50.f;
    const float MAX_TRACK_HEIGHT = 500.f;

    const int MAX_POINTS_PER_WAVEFORM = 10000;

    const float GAIN_MIN = -100;
    const float GAIN_MAX = +40;

    // drawing state on current frame

    ImVec2 canvas_pos;      ///< upper-left corner of whole timeline window
    ImVec2 full_canvas_size;
    ImVec2 global_canvas_pos; ///< canvas pos without YScroll


    ImVec2 field_pos;       ///< upper-left corner of timeline, not including track info, canvas_pos + {track_info_width, 0}
    ImVec2 global_field_pos; ///< field pos without YScroll
    ImVec2 field_size;      ///< size of timeline without track_info

    ImVec2 mouse_pos;       ///< absolute mouse cursor position

    bool   hovered_all;     ///< mouse cursor is on timeline window
    bool   hovered;         ///< mouse cursor is on timeline
    bool   hovered_on_bg;   ///< mouse cursor is on timeline background
    bool   clicked;         ///< left mouse button was clicked && hovered
    bool   clicked_on_bg;   ///< left mouse button was clicked on empty space
    bool   focused;         ///< timeline window is focused

    TimelineInteraction interaction;

    std::unordered_map<ClipId_t, ClipView> clip_view;
    std::unordered_map<TrackId_t, TrackView> track_view;

    TimelineClipboard clipboard;

    TimeLine &timeline_; /// < Viewed timeline
    PluginManager &plugin_manager_;
public:

    TimelineView(TimeLine &timeline, PluginManager &plugin_manager, float scale):
        timeline_(timeline),
        plugin_manager_(plugin_manager),
        pixels_per_frame(scale), scroll_frame(0)
    {}

    // === Clips and tracks view getters
    ClipView &getClipView(const Clip& clip) { return getClipView(clip.id); }

    ClipView &getClipView(ClipId_t id) {
        auto it = clip_view.find(id);
        if (it == clip_view.end() ) {
            if (!timeline_.isValidClipId(id)) {
                PLOG_ERROR << "Invalid clip id";
            }

            clip_view[id] = clip_view_default;
            return clip_view[id];
        }
        else return it->second;
    }

    TrackView &getTrackView(const Track& track) { return getTrackView(track.id); }

    TrackView &getTrackView(TrackId_t id) {
        auto it = track_view.find(id);
        if (it == track_view.end()) {
            if (!timeline_.isValidTrackId(id)) {
                PLOG_ERROR << "Invalid track id";
            }
            track_view[id] = track_view_default;
            return track_view[id];
        }
        else return it->second;
    }

    // === Various conversion functions

    // Кадр -> позиция в пикселях (относительно левого края канваса)
    float frameToPixel(ma_int64 frame) const {
        return static_cast<float>(frame - scroll_frame) * pixels_per_frame;
    }

    float frameToPixelRel(ma_uint64 frame) const {
        return static_cast<float>(frame) * pixels_per_frame;
    }

    // Пиксель -> кадр
    ma_int64 pixelToFrame(float pixel_x) const {
        return scroll_frame + static_cast<ma_int64>(pixel_x / pixels_per_frame);
    }

    ma_int64  pixelToFrameRel(float pixel_x) const {
        return static_cast<ma_uint64>(pixel_x / pixels_per_frame);
    }

    // convert frame to time in milliseconds
    float frameToMillis(ma_uint64 frame) const {
        const float MILLIS_PER_SEC = 1000;
        return static_cast<float>(frame) / INNER_SAMPLE_RATE * MILLIS_PER_SEC;
    }

    std::pair<ma_int64, ma_int64> getVisibleFramesRange() const {
        return {pixelToFrame(0), pixelToFrame(field_size.x)};
    }

    std::pair<ma_int64, float> getNearestBeatInPixels() const {
        // |  scroll  |       |
        const ma_uint64 step = getBeatStepInFrames();
        ma_int64 beat_idx = (scroll_frame + step - 1) / step;
        return {beat_idx, frameToPixel(beat_idx*step)};
    }

    // ImRect getClipRect(ImVec2 pos, float height, ma_uint64 left_frame, ma_uint64 right_frame) {
    //     ImVec2 start(pos.x + frameToPixel(left_frame), pos.y);
    //     ImVec2 end(pos.x + frameToPixel(right_frame), pos.y + track_height);
    //     return ImRect(start, end);
    // }

    ImU32 getGridLineCol() const {
        return col_grid_line;
    }

    ma_uint64 getBeatStepInFrames() const {
        return beats_per_second * INNER_SAMPLE_RATE;
    }

    float getBeatStepInPixels() const {
        return getBeatStepInFrames() * pixels_per_frame;
    }


    std::pair<ma_int64, ma_int64> getVisibleClipRange(const Clip& clip) const {
        auto [vis_left, vis_right] = getVisibleFramesRange();
        ma_int64 left = std::max(clip.timeline_start_frame, vis_left);
        ma_int64 right = std::min(vis_right, clip.getTimelineEndFrame());

        return {left, right};
    }

    ImRect getFullClipRect(ImVec2 track_start_pos, const Clip& clip) const {
        return ImRect{track_start_pos.x + frameToPixel(clip.timeline_start_frame),
                      track_start_pos.y,
                      track_start_pos.x + frameToPixel(clip.getTimelineEndFrame()),
                      track_start_pos.y + track_height};
    }
    // === УТИЛИТЫ ДЛЯ ЗУМА И СКРОЛЛА ===

    void zoomAtPixel(float pixel_x, float zoom_factor);

    void scrollByFrames(int64_t delta_frames);

    std::pair<int, int64_t> mousePosToTrackAndFrame();

    // ====
private:
    // general work with timeline
    void removeClipFromTimeline(ClipId_t id);
    ClipId_t addClipToTimeline(const Clip& clip, int tr_idx, std::optional<ClipView> style = std::nullopt);

    // clipboard
    void copyToClipboard(ClipId_t id);
    void cutToClipboard(ClipId_t id);  // uses mtx

    std::optional<ClipboardPayload> pasteFromClipboard();

    // ====

    std::pair<bool, bool> HandleClipBaseInteraction(const Clip& clip);
    bool HandleClipTrimStretchInteraction(bool right, Clip& clip);
    bool HandleClipTrim(ClipId_t id);
    bool HandleClipStretch(ClipId_t id);

    bool HandleHorizontalClipDrag(ClipId_t clip_id, ImVec2 mouse_delta);
    bool HandleVerticalClipDrag(ClipId_t clip_id);


    void HandleInteractions(PlaybackController& playback);


    // ======== DRAWING ==============

    void DrawFxMenu(EffectChain &effect_chain);

    void DrawTimeGrid(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size);

    void DrawTrack(Track& track, bool parity);
    // Track owns draw list for clip and waveform
    void DrawClip(ImDrawList* draw_list, Clip& clip, ImVec2 track_start_pos);

    void DrawMiniWaveform(ImDrawList* draw_list, const Clip& clip,
                      ImRect waveform_rect);

    void DrawPlayHead(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 size);

public:
    void DrawTimeline(PlaybackController& playback);

};




} // namespace waves
