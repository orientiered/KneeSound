#include "audio_effects.h"
#include "common.h"

#include "timeline_view.h"
#include "effects/biquad_filter.h"
#include "effects/fft_equalizer.h"
#include "imgui_misc.h"

#include "playback_controller.h"
#include <imgui.h>
#include <memory>

namespace waves {

// ========== SMALL UTILS ==================


void TimelineView::zoomAtPixel(float pixel_x, float zoom_factor) {
    float new_ppf = pixels_per_frame * zoom_factor;
    if (new_ppf > MAX_PPF || new_ppf < MIN_PPF) {
        return;
    }

    ma_int64 frame_under_cursor = pixelToFrame(pixel_x);
    // Увеличиваем масштаб, сохраняя позицию под курсором
    pixels_per_frame = new_ppf;
    // Корректируем скролл, чтобы кадр под курсором остался на месте
    // Scroll can't be less that zero
    scroll_frame = std::max(0ll, frame_under_cursor - static_cast<ma_int64>(pixel_x / pixels_per_frame));
}

void TimelineView::scrollByFrames(int64_t delta_frames) {
    PLOG_DEBUG << "Scrolling timeline by " << delta_frames << " frames";
    if (delta_frames > 0) {
        scroll_frame += delta_frames;
    } else {
        scroll_frame = (scroll_frame > static_cast<ma_uint64>(-delta_frames))
            ? scroll_frame + delta_frames : 0;
    }
}

std::pair<int, int64_t> TimelineView::mousePosToTrackAndFrame() {
    int track_idx = (mouse_pos.y - field_pos.y - grid_line_header) / track_height;

    int64_t start_frame = 0;
    if ((mouse_pos.x - field_pos.x) >= 0)
        start_frame = pixelToFrame(mouse_pos.x - field_pos.x);

    return std::make_pair(track_idx, start_frame);
}

// ====================== CLIP DRAWING ==================

//! Assuming that clip_timeline frames are visible
void TimelineView::DrawMiniWaveform(ImDrawList* draw_list, const Clip& clip,
                      ImRect waveform_rect) {
    if (!clip.source || clip.source->pcmData.empty()) return;

    float center_y = waveform_rect.GetCenter().y;
    float height = waveform_rect.GetHeight();

    float start_x = waveform_rect.GetTL().x;
    float end_x   = waveform_rect.GetTR().x;
    float width   = waveform_rect.GetWidth();

    auto [timeline_left_frame, timeline_right_frame] = getVisibleClipRange(clip);

    // timeline_clip_frames.first - clip.timeline_start_frame + clip.source_start_frame;
    assert(clip.timelineToClipFrame(timeline_left_frame));
    ma_uint64 clip_start_frame = *clip.timelineToClipFrame(timeline_left_frame);
    // timeline_clip_frames.second - clip.timeline_start_frame + clip.source_start_frame;
    assert(clip.timelineToClipFrame(timeline_right_frame-1));

    ma_uint64 clip_end_frame  = *clip.timelineToClipFrame(timeline_right_frame-1) + 1;

    // Calculating frame step
    // At least one frame or 1 pixel
    int step = std::max(1, static_cast<int>((clip_end_frame-clip_start_frame) / width));
    // generating waveform

    ImVec2 prev_point = ImVec2(start_x, center_y);

    ClipView &style = getClipView(clip);

    //TODO: сделать так, чтобы вид вэйвформы не менялся при смещении и изменении масштаба
    if (!g_debug_flags.preview_new_waveform || (step == 1)) {

        for (ma_uint64 f = clip_start_frame; f < clip_end_frame; f += step) {
            float x = start_x + frameToPixelRel(f - clip_start_frame);
            // if (x > canvas_width) break;

            float sample = clip.getMonoClipFrame(f) * style.gain_waveform;
            float y = center_y - sample * (height / 2) * 0.9f; // 0.8 для отступа

            draw_list->AddLine(prev_point, ImVec2(x, y), style.col_waveform, 2.f);
            prev_point = ImVec2(x, y);
        }

    } else {


        int px_step = 1;
        for (int px = static_cast<int>(start_x); px < static_cast<int>(end_x); px += px_step) {
        // for (int px = static_cast<int>(start_x); px < static_cast<int>(end_x); ++px) {
            float x = static_cast<float>(px) + 0.5f;
            float rel = (x - start_x) / width;

            ma_uint64 f = clip_start_frame + static_cast<ma_uint64>(rel * (clip_end_frame - clip_start_frame));

            // Берём не один сэмпл, а мин/макс в радиусе ±1 пикселя
            // float min_v = 1.0f, max_v = -1.0f;
            ma_uint64 radius = std::max<ma_uint64>(1, (clip_end_frame - clip_start_frame) / width * px_step);
            auto [min_v, max_v] = clip.getPeak(f, f+radius);

            min_v *= style.gain_waveform;
            max_v *= style.gain_waveform;

            float min_y = center_y - max_v * (height/2) * 0.9f;
            float max_y = center_y - min_v * (height/2) * 0.9f;
            draw_list->AddLine(ImVec2(px, min_y), ImVec2(px, max_y), style.col_waveform, px_step);
            // draw_list->AddLine(ImVec2(px, min_y), ImVec2(px, max_y), style.col_waveform, 1.0f);
        }

    }
}

void TimelineView::DrawClip(ImDrawList* draw_list, Clip& clip,
    ImVec2 track_start_pos) {

    /*
    start
    ------------------------
    | Label     FFT M Eff  |
    ------------------------
    |                      |
    |   waveform           |
    |                      |
    ------------------------ end
    */
    // absolute frames on timeline
    auto [clip_left, clip_right] = getVisibleClipRange(clip);
    // clip is not visible, skipping

    ClipView &style = getClipView(clip);

    float x_start = track_start_pos.x + frameToPixel(clip_left);
    float x_end = track_start_pos.x + frameToPixel(clip_right);
    // TODO: draw two channels
    float y_top = track_start_pos.y + clip_vert_pad;
    float y_bottom = track_start_pos.y + track_height - clip_vert_pad;

    // ImGui::SetCursorScreenPos(ImVec2{x_start, y_top});
    // ID_GUARD(&clip + 5, ImGui::Text("Aboba"););

    if (clip_left >= clip_right) return;

    ImVec2 start(x_start, y_top), end(x_end, y_bottom);
    ImRect full_clip_rect = getFullClipRect(track_start_pos, clip);

    /* ============== Clickable base =========================== */

    ImGui::SetNextItemAllowOverlap();  // base of the clip may be overlapped by widgets
    ImGui::CursorGuard cg(start); // setting cursor and saving previous position
    ID_GUARD(&clip.id, ImGui::InvisibleButton("##Clickable", end-start););
    auto [is_hovered, is_selected] = HandleClipBaseInteraction(clip);

    // Mimicking selectable
    ImU32 color_base = is_selected ? style.col_clip_selected
                                   : style.col_clip_base;
    ImU32 color_border = is_hovered ? IM_COL32(255, 255, 255, 255)
                                    : IM_COL32(255, 255, 255, 150);

    // Drawing rectangle over clip
    draw_list->AddRectFilled(start, end, color_base, 3.0f);
    draw_list->AddRect(start, end, color_border, 3.0f);


    // Predifinitions
    float text_pad = 2;
    float text_horizontal_pad = 10;
    float text_height = ImGui::GetFrameHeight();
    float bar_height = text_height + 2 * text_pad;

    // Trim bars

    ImVec2 trim_bar_size{10, track_height - bar_height};

    auto [left_visible, right_visible] = getVisibleFramesRange();
    bool allow_trim = (x_end - x_start) > trim_bar_size.x * 5;
    auto draw_clip_trim = [&] (bool right, ImVec2 pos, const char *name) {
        ImGui::SetCursorScreenPos(pos);
        ID_GUARD((uint8_t*)&clip.id + 1 + right,
            ImGui::InvisibleButton(name, trim_bar_size););

        if (HandleClipTrimStretchInteraction(right, clip)) {
            draw_list->AddRectFilled(pos, pos + trim_bar_size, style.col_clip_selected, 2);
        }
    };

    if (allow_trim) {
        if (clip_left > left_visible) {
            ImVec2 pos = {x_start - trim_bar_size.x / 2, y_top + bar_height};
            draw_clip_trim(false, pos, "##LeftTrim");
        }

        if (clip_right < right_visible) {
            ImVec2 pos = {x_end - trim_bar_size.x / 2, y_top + bar_height};
            draw_clip_trim(true, pos, "##RightTrim");
        }
    }

    // ==================== HEADER: LABEL AND EFFECT BUTTONS ======================

    float header_left_cursor = start.x;
    float header_right_cursor = end.x - text_horizontal_pad;

    float bar_y = y_top + text_pad;

    auto advance_cursor_for_text_left = [&](const char *text) {
        float text_size = ImGui::CalcTextSize(text).x + 2 * text_horizontal_pad;
        if (header_right_cursor - text_size > header_left_cursor) {
            header_right_cursor -= text_size;
            ImGui::SetCursorScreenPos({header_right_cursor, bar_y});
            // ImGui::SetNextItemWidth(text_size);
            return true;
        }

        return false;
    };

    const char * const CLIP_POPUP = "CLIP_POPUP_OPTS";


    bool more_button = advance_cursor_for_text_left("...");
    if (more_button) {
        ImGui::PushID(&clip);
        if (ImGui::Button("...")) {
            ImGui::OpenPopup(CLIP_POPUP);
        }
    }

    // clip name
    std::string label = style.name;
    ImVec2 label_size = ImGui::CalcTextSize(label.c_str());

    if (ImGui::BeginPopup(CLIP_POPUP)) {
        ImGui::SeparatorText(label.c_str());
        ImGui::Checkbox("Mute", &clip.muted);
        ImGui::DragFloat("Gain", &clip.gain_db, 0.3, GAIN_MIN, GAIN_MAX, "%.1f");
        ImGui::DragFloat("Pan", &clip.pan, 0.01, -1, 1, "%.2f");

        float fade_in_out_sec[2] =
            {frameToSec(clip.fade_in.duration), frameToSec(clip.fade_out.duration)};
        if (ImGui::DragFloat2("Fade in/out", fade_in_out_sec, 0.01, 0, clip.getDurationSec())) {
            clip.fade_in.duration = secToFrame(fade_in_out_sec[0]);
            clip.fade_out.duration = secToFrame(fade_in_out_sec[1]);
        }

        float playback_speed = clip.playback_speed;
        if (ImGui::DragFloat("Time-stretch", &playback_speed, 0.05, Clip::MIN_PLAYBACK_SPEED, Clip::MAX_PLAYBACK_SPEED)) {
            clip.setPlaybackSpeed(playback_speed);
        }

        if (ImGui::Button("FFT")) {
            analyzer.analyzeClip(clip);
        }


        static ImVec4 color;
        color = ImGui::ColorConvertU32ToFloat4(style.col_clip_base);

        if (ImGui::ColorEdit4("Base color", (float*)&color, ImGuiColorEditFlags_NoInputs)) {
            style.col_clip_base = ImGui::ColorConvertFloat4ToU32(color);
        }

        ImGui::DragFloat("Waveform gain", &style.gain_waveform, 0.01, 0, 5);
        ImGui::EndPopup();
    }

    if (more_button) ImGui::PopID();

    if (header_left_cursor + label_size.x < header_right_cursor) {
        draw_list->AddText(start + ImVec2{text_pad,text_pad}, style.col_clip_text, label.c_str());
        header_left_cursor += label_size.x;
    }


    // ====================== WAVEFORM =======================
    ImRect full_waveform_rect = full_clip_rect;
    full_waveform_rect.Min += ImVec2{0, bar_height};

    draw_list->AddLine(ImVec2{x_start,y_top + bar_height},
                       ImVec2{x_end,  y_top + bar_height}, color_border, 1);

    ImVec2 waveform_start = start + ImVec2{0, bar_height};
    draw_list->PushClipRect(waveform_start, end, true);
    float waveform_height = track_height - bar_height;
    // Drawing waveform
    if (clip.source && (x_end - x_start) > 20) {
        DrawMiniWaveform(draw_list, clip,
                ImRect{waveform_start, end});
    }

    // ======================= Fade in/out ======================

    if (clip.fade_in.duration > 0) {
        draw_list->AddTriangleFilled(full_waveform_rect.GetTL(), full_waveform_rect.GetBL(),
                                    full_waveform_rect.GetTL() + ImVec2{frameToPixelRel(clip.fade_in.duration), 0},
                                     col_clip_fade);
    }

    if (clip.fade_out.duration > 0) {
            draw_list->AddTriangleFilled(full_waveform_rect.GetTR(), full_waveform_rect.GetBR(),
                                    full_waveform_rect.GetTR() - ImVec2{frameToPixelRel(clip.fade_out.duration), 0},
                                     col_clip_fade);
    }
    draw_list->PopClipRect();
}

void TimelineView::DrawTimeGrid(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {

    float step = getBeatStepInPixels();
    float minor_step = step / grid_minor_lines_per_major;

    auto [beat_idx, current_rel_x] = getNearestBeatInPixels();
    float y_start = canvas_pos.y;
    float y_end   = canvas_pos.y + canvas_size.y;

    int visible_major_lines = (canvas_size.x - current_rel_x) / step;

    bool high_scale = visible_major_lines > grid_line_count_limit; // showing only major lines

    int minor_counter = 0;

    // finding start position
    current_rel_x -= step;
    while (current_rel_x < 0) {
        current_rel_x += minor_step;
        minor_counter = (minor_counter+1) % grid_minor_lines_per_major;
    }

    while (current_rel_x < canvas_size.x) {
        float current_x = canvas_pos.x + current_rel_x;

        if (high_scale)
            draw_list->AddLine(ImVec2{current_x, y_start}, ImVec2{current_x, y_end},
                col_grid_line, thickness_grid_line_minor);
        else {
            float thick = (minor_counter == 0) ? thickness_grid_line_major: thickness_grid_line_minor;
            draw_list->AddLine(ImVec2{current_x, y_start}, ImVec2{current_x, y_end},
                col_grid_line, thick);
        }

        if ((minor_counter == 0 || high_scale) && beat_idx % time_signature == 0) {
            std::string beat_str = std::to_string(beat_idx);
            draw_list->AddText(ImVec2{current_x, y_start}, col_grid_line_text, beat_str.c_str());
        }

        if (high_scale) {
            current_rel_x += step;
            beat_idx++;
        } else {
            if (minor_counter == 0)
                beat_idx++;
            minor_counter = (minor_counter + 1) % grid_minor_lines_per_major;
            current_rel_x += minor_step;
        }
    }

}

/* =================== CLIP INTERACTIONS: CLICKING, TRIMMING, DRAGGING ==================== */

bool TimelineView::HandleClipTrimStretchInteraction(bool right, Clip& clip) {
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool active  = ImGui::IsItemActive();
    bool alt_pressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);

    if (active) {
        if (ImGui::BeginTooltip()) {
            const char *action = (interaction.mode == TimelineInteraction::Mode::TrimmingClip) ? "trim" : "stretch";
            ImGui::Text("Drag to %s clip", action);

            auto [start_min, start_sec] = frameToMinSec(clip.timeline_start_frame);
            auto [end_min, end_sec] = frameToMinSec(clip.getTimelineEndFrame());

            ImGui::Text("Start: %d:%.3f", start_min, start_sec);
            ImGui::Text("End: %d:%.3f", end_min, end_sec);

            ImGui::EndTooltip();
        }
    } else if (hovered) {
        if (ImGui::BeginTooltip()) {

            const char *text = (alt_pressed) ? "Stretch" : "Trim";
            ImGui::Text("%s", text);
            ImGui::EndTooltip();
        }
    }

    if (clicked) {
        if (alt_pressed) {
            PLOG_DEBUG << "Stretch action start on clip " << clip.id;
            interaction.stretched_clip_id = clip.id;
            interaction.stretching_right = right;
            interaction.mode = TimelineInteraction::Mode::StretchingClip;
        } else {
            PLOG_DEBUG << "Trim action start on clip " << clip.id;
            interaction.trimmed_clip_id = clip.id;
            interaction.trimming_right = right;
            interaction.mode = TimelineInteraction::Mode::TrimmingClip;
        }
    }

    return hovered || active;
}

bool TimelineView::HandleClipTrim(ClipId_t id) {
    Clip *clip = timeline_.getClipById(id);

    if (clip) {
        return clip->trim(interaction.trimming_right, mousePosToTrackAndFrame().second);
    }

    return false;
}

bool TimelineView::HandleClipStretch(ClipId_t id) {
    Clip *clip = timeline_.getClipById(id);

    if (clip) {
        return clip->stretch(interaction.stretching_right, mousePosToTrackAndFrame().second);
    }

    return false;
}


//! call immediately after clip's base invisible button
// @return Pair of bools: is clip hovered, is clip selected
std::pair<bool, bool> TimelineView::HandleClipBaseInteraction(const Clip& clip) {

    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        interaction.hovered_clip_id = clip.id;
    }

    // selecting clip on click
    //TODO: check click at the edge of the clip -> resize
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        PLOG_DEBUG << "Selected clip " << clip.id;
        interaction.selected_clip_id = clip.id;
        interaction.mouse_start_pos = mouse_pos;
        interaction.drag_start_frame = clip.timeline_start_frame;
        interaction.mode = TimelineInteraction::Mode::DraggingClip;
    }

    return {hovered, interaction.selected_clip_id == clip.id};
}


bool TimelineView::HandleHorizontalClipDrag(ClipId_t clip_id, ImVec2 mouse_delta) {
    // Конвертируем смещение в пикселях в кадры
    Clip* clip = timeline_.getClipById(clip_id);
    if (!clip) return false;

    int64_t frame_delta = pixelToFrameRel(mouse_delta.x);

    if (frame_delta != 0) {
        PLOG_DEBUG << "Dragging clip " << clip_id << " by " << frame_delta << "frames";
        // Проверяем границы проекта
        if (frame_delta < 0) {
            clip->timeline_start_frame = std::max(static_cast<int64_t>(0), frame_delta + (int64_t)clip->timeline_start_frame);
            return true;
        } else if (frame_delta > 0) {
            clip->timeline_start_frame += frame_delta;

            return true;
        }
    }
    return false;
}

bool TimelineView::HandleVerticalClipDrag(ClipId_t clip_id) {
    auto  current_track_idx = timeline_.getTrackIdx(clip_id);
    if (!current_track_idx) return false;


    int expected_track_idx = mousePosToTrackAndFrame().first;

    if (expected_track_idx < 0 || expected_track_idx == *current_track_idx) return false;

    PLOG_DEBUG << "Moving clip " << clip_id << " from track " << *current_track_idx
               << " to track " << expected_track_idx;

    timeline_.moveClipToTrack(clip_id, expected_track_idx);
    return true;
}


/* ======================== DRAWING ======================================= */

void TimelineView::DrawFxMenu(std::vector<EffectSlot> &effects) {
    ImGui::SeparatorText("Effects");

    // Adapted from ImGui Demo
    ImGui::PushItemFlag(ImGuiItemFlags_AllowDuplicateId, true);

    const char * const EFFECT_SETTINGS_POPUP = "EFFECT_SETTINGS_POPUP";

    // Simple reordering
    size_t len = effects.size();
    int erase_idx = -1;

    for (int n = 0; n < len; n++)
    {
        ImGui::IdGuard ig(n);
        ImGui::Selectable(effects[n].name.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups | ImGuiSelectableFlags_AllowOverlap);

        bool is_dragged = ImGui::IsItemActive() && !ImGui::IsItemHovered();

        ImGui::SameLine();

        if (ImGui::Button("...")) {
            ImGui::OpenPopup(EFFECT_SETTINGS_POPUP);
        }

        if (ImGui::BeginPopup(EFFECT_SETTINGS_POPUP)) {
            effects[n].view->DrawSettings();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("X")) {
            erase_idx = n;
        }

        if (is_dragged)
        {
            int n_next = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
            if (n_next >= 0 && n_next < len)
            {
                std::swap(effects[n], effects[n_next]);
                ImGui::ResetMouseDragDelta();
            }
        }

    }

    if (erase_idx >= 0) {
        //TODO: add deletion
    }

    ImGui::PopItemFlag();

    const char * const ADD_EFFECT_POPUP = "ADD_EFFECT_POPUP";
    if (ImGui::Button("Add effect")) {
        ImGui::OpenPopup(ADD_EFFECT_POPUP);
    }

    if (ImGui::BeginPopup(ADD_EFFECT_POPUP)) {
        // TODO: VERY BAD DESIGN
        if (ImGui::Button("Biquad filter")) {
            std::unique_ptr<BiquadFilter> kernel = std::make_unique<BiquadFilter>();
            std::unique_ptr<IEffectView> view = std::make_unique<BiquadSettings>(kernel.get());
            effects.emplace_back(std::unique_ptr<IDspKernel>(std::move(kernel)), std::move(view));
            effects.back().name = "Biquad filter";
            ImGui::CloseCurrentPopup();
        }
        // TODO: HARDCODED Block size
        if (ImGui::Button("FFT Equalizer")) {
            std::unique_ptr<FFT_Equalizer> kernel = std::make_unique<FFT_Equalizer>(RENDER_BLOCK_SIZE);
            std::unique_ptr<IEffectView> view = std::make_unique<FFT_EqualizerView>(kernel.get());
            effects.emplace_back(std::unique_ptr<IDspKernel>(std::move(kernel)), std::move(view));
            effects.back().name = "FFT Equalizer";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


// void TimelineView::DrawPitchSettings(bool *enable, PitchShifter &pitch_shift) {
//     float stretch = pitch_shift.getStretch();

//     if (ImGui::DragFloat("Stretch", &stretch, 0.05, 0.1, 10)) {
//         pitch_shift.setStretch(stretch);
//     }
// }



void TimelineView::DrawTrack(Track& track, bool parity) {

    TrackView &style = getTrackView(track);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, parity ? style.col_track_bg_even : style.col_track_bg_odd);

    // const float mult = 0.99;
    ImGui::PushID(&track);
    ImGui::BeginChild("Track_canvas", ImVec2(0, track_height), 0,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 mouse_pos = ImGui::GetMousePos();

    /* ====== TRACK INFO AND CONTROLS ====================== */
    ImGui::BeginChild("Track info", ImVec2{track_info_width, track_height}, 0);

    // track name
    ID_GUARD(&track.id, ImGui::InputText("", &style.name); );

    const char * const FX_MENU_POPUP = "FX_MENU_POPUP";
    ID_GUARD(&track.enable_eq,

        if (ImGui::Button("Fx"))
            ImGui::OpenPopup(FX_MENU_POPUP);

        if (ImGui::BeginPopup(FX_MENU_POPUP)) {
            DrawFxMenu(track.effects_);
            ImGui::EndPopup();
        }

    );

    ImGui::SameLine();
    // mute
    ID_GUARD(&track.mute, ImGui::Checkbox("Mute", &track.mute););


    // gain

    ID_GUARD(&track.gain_db,
        ImGui::DragFloat("Gain", &track.gain_db, 0.3, GAIN_MIN, GAIN_MAX, "%.1f");
    );


    ID_GUARD(&track.rendering_buffer,
        if (ImGui::Button("FFT Spectr")) {
            analyzer.subscribeToBuffer(&track.rendering_buffer);
        }
    );

    ImGui::EndChild();
    /* ========================================= */

    canvas_pos += ImVec2{track_info_width + track_pad, 0};

    // === Timeline part

    // ===== Drawing clips =====
    for (Clip& clip: track.clips) {
        DrawClip(draw_list, clip, canvas_pos);

    }

    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();
    // empty track space is considered background
    clicked_on_bg |= ImGui::IsItemClicked(ImGuiMouseButton_Left);
    hovered_on_bg |= ImGui::IsItemHovered();

}

void TimelineView::DrawPlayHead(ImDrawList *draw_list,
                                ImVec2 canvas_pos, ImVec2 size) {

    ma_uint64 playhead_frame = timeline_.playhead_frame.load();
    if (playhead_frame >= scroll_frame) {
        float playhead_x = canvas_pos.x + frameToPixel(playhead_frame);
        draw_list->AddLine(ImVec2(playhead_x, canvas_pos.y),
                          ImVec2(playhead_x, canvas_pos.y + size.y),
                          col_playhead, 2.0f);
    }
}


/* ================= General work with timeline ============ */

void TimelineView::removeClipFromTimeline(ClipId_t id) {
    PLOG_DEBUG << "Deleting clip " << id;
    timeline_.removeClipById(id);
    clip_view.erase(id);

    // unselecting deleted clip
    if (interaction.selected_clip_id == id)
        interaction.selected_clip_id = CLIP_NONE;
}

ClipId_t TimelineView::addClipToTimeline(const Clip& clip, int track_idx, std::optional<ClipView> style) {
    PLOG_DEBUG << "Adding clip " << clip.id << "to track " << track_idx;
    ClipId_t id = timeline_.addClip(clip, track_idx);
    if (id == CLIP_NONE) return id;

    if (!style) {
        clip_view[id] = clip_view_default;
        clip_view[id].name = clip.source->name;
    } else {
        clip_view[id] = *style;
    }

    return id;
}


/* ================= Clipboard ================== */

void TimelineView::copyToClipboard(ClipId_t id) {
    PLOG_DEBUG << "Copying clip to clipboard " << id;

    Clip *clip = timeline_.getClipById(id);
    if (!clip) return;

    clipboard.data = clip->copy();
    clipboard.style = getClipView(id);
}

void TimelineView::cutToClipboard(ClipId_t id) {
    PLOG_DEBUG << "Cutting clip to clipboard " << id;

    Clip *clip = timeline_.getClipById(id);
    if (!clip) return;

    // copying without changing id
    clipboard.data = *clip;
    clipboard.style = getClipView(id);
    clip_view.erase(id);

    removeClipFromTimeline(id);
}

std::optional<ClipboardPayload> TimelineView::pasteFromClipboard() {
    if (!clipboard.data) return std::nullopt;
    // always copying
    return ClipboardPayload{clipboard.data->copy(), *clipboard.style};
}

/* ========================== Interaction ================================ */

void TimelineView::HandleInteractions(PlaybackController& playback) {

    // 0 ~~ Mouse on empty space ~~
    if (hovered_on_bg) {
        interaction.hovered_clip_id = CLIP_NONE;
    }

    // click on empty space
    if (clicked_on_bg) {
        if (interaction.selected_clip_id != CLIP_NONE) {
            PLOG_DEBUG << "Unselected clip " << interaction.selected_clip_id;
        }
        interaction.selected_clip_id = CLIP_NONE;
    }

    // 1 ~~ Mouse released -> reset interaction ~~
    if (focused && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (interaction.mode != TimelineInteraction::Mode::None) {
            //TODO: add interaction to history
        }
        interaction.mode = TimelineInteraction::Mode::None;
        PLOG_DEBUG << "Timeline interaction stop";
    }


    // 2 Playhead moving handling
    if (clicked_on_bg) {
        timeline_.playhead_frame.store(pixelToFrame(mouse_pos.x - field_pos.x));
    }

    // 3 Dragging handling
    if (interaction.selected_clip_id != CLIP_NONE &&
        interaction.mode == TimelineInteraction::Mode::DraggingClip &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (HandleHorizontalClipDrag(interaction.selected_clip_id, delta)) {
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        HandleVerticalClipDrag(interaction.selected_clip_id);

    }

    // 3.1 Clip trimming

    if (interaction.trimmed_clip_id != CLIP_NONE &&
        interaction.mode == TimelineInteraction::Mode::TrimmingClip)
    {
        HandleClipTrim(interaction.trimmed_clip_id);
    }

    // 3.2 Clip stretching

    if (interaction.stretched_clip_id != CLIP_NONE &&
        interaction.mode == TimelineInteraction::Mode::StretchingClip)
    {
        HandleClipStretch(interaction.stretched_clip_id);
    }

    // 4 Clip deletion
    if (interaction.selected_clip_id != CLIP_NONE &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        removeClipFromTimeline(interaction.selected_clip_id);
    }

    // 5 Clip cutting
    bool keyCtrl_pressed = ImGui::GetIO().KeyCtrl;
    bool keyShift_pressed = ImGui::GetIO().KeyShift;
    float mouseWheel_delta = ImGui::GetIO().MouseWheel;

    if (interaction.selected_clip_id != CLIP_NONE &&
        !keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_X)) {
        PLOG_DEBUG << "Cutting clip " << interaction.selected_clip_id;

        Clip *selected = timeline_.getClipById(interaction.selected_clip_id);
        auto clip_loc = timeline_.getTrackAndClipIdx(interaction.selected_clip_id);

        if (!selected) {
            PLOG_ERROR << "SELECTED CLIP " << interaction.selected_clip_id << " that is not present on timeline";
        }

        std::optional<Clip> new_clip = selected->cut(timeline_.playhead_frame);
        if (new_clip)
            addClipToTimeline(*new_clip, clip_loc->track_idx, getClipView(*selected));
    }

    // 6 Clip copy-cut-pasting

    // Ctrl + C
    if (interaction.selected_clip_id != CLIP_NONE &&
        keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_C)) {
        PLOG_DEBUG << "Copying clip " << interaction.selected_clip_id;
        copyToClipboard(interaction.selected_clip_id);
    }

    // Ctrl + X
    if (interaction.selected_clip_id != CLIP_NONE &&
        keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_X)) {
        PLOG_DEBUG << "Copying clip " << interaction.selected_clip_id;
        cutToClipboard(interaction.selected_clip_id);
    }

    // Ctrl + V

    if (keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_V) && hovered ) {
        PLOG_DEBUG << "Pasting clip";

        auto loc = mousePosToTrackAndFrame();

        std::optional<ClipboardPayload> clip_opt = pasteFromClipboard();
        if (clip_opt) {
            auto [clip, style] = *clip_opt;
            clip.timeline_start_frame = loc.second;
            addClipToTimeline(clip, loc.first, style);
        }
    }

    // 7 Play/pause
    if (focused && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        playback.handleToggleFromTimeline();
    }

    // 8 ===  Handling scroll and zoom ===

    if (hovered && mouseWheel_delta != 0) {
        if (keyCtrl_pressed) {
            float mouse_x_rel = mouse_pos.x - field_pos.x;
            zoomAtPixel(mouse_x_rel, mouseWheel_delta > 0 ? 1.1f : 0.9f);
        } else if (keyShift_pressed) {
            track_height *= mouseWheel_delta > 0 ? 1.1f : 0.9f;
        } else {
            float pixels_per_mouse_scroll = 50;
            float pixel_delta = (mouseWheel_delta > 0 ? 1.f: -1.f) * pixels_per_mouse_scroll;

            scrollByFrames(pixelToFrameRel(pixel_delta));

        }
    }

}

void TimelineView::DrawTimeline(PlaybackController& playback) {

    // ImGui::SetNextWindowContentSize(ImVec2(1e6, 0));
    // Timeline over all available space
    ImGui::BeginChild("Timeline_canvas", ImVec2(0, 0), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar);

    // === 0. Updating drawing state variables

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    canvas_pos = ImGui::GetCursorScreenPos();
    global_canvas_pos = ImGui::GetWindowPos();
    full_canvas_size = ImGui::GetContentRegionAvail();

    /*    track_info | timeline         */

    global_field_pos = ImGui::GetWindowPos() + ImVec2{track_info_width, 0};
    field_pos = canvas_pos + ImVec2{track_info_width, 0};
    field_size = full_canvas_size - ImVec2{track_info_width, 0};

    // Invisible button that detects clicks on empty space
    {
        ImGui::CursorGuard cg(global_field_pos); // setting cursor and saving previous position
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##Timeline_background", field_size);
        clicked_on_bg = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        hovered_on_bg = ImGui::IsItemHovered();
    }

    mouse_pos = ImGui::GetMousePos();
    // mouse is in timeline zone
    hovered_all = ImRect(global_canvas_pos, canvas_pos + full_canvas_size).Contains(mouse_pos);
    hovered = ImRect(global_field_pos, field_pos + field_size).Contains(mouse_pos);
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);


    // === 1. Drawing tracks
    ImGui::SetCursorScreenPos(canvas_pos + ImVec2{0,grid_line_header});
    for (int idx = 0; idx < timeline_.getTrackCount(); idx++) {
        DrawTrack(timeline_.getTrack(idx), idx%2);

    }

    ImGui::SetCursorScreenPos(canvas_pos);
    // === 2. Drawing time grid ===
    DrawTimeGrid(draw_list, global_field_pos, field_size);
    // DrawTimeGrid(draw_list, field_pos, field_size);


    // === 3. Курсор воспроизведения ===
    DrawPlayHead(draw_list, global_field_pos, field_size);

    // === 4. Interaction ========================

    HandleInteractions(playback);

    // ===  Bottom Slider ========================

    ImGui::SetCursorScreenPos(global_field_pos + ImVec2{0, field_size.y - ImGui::GetTextLineHeightWithSpacing()});
    ImGui::SetNextItemWidth(field_size.x);
    {
        ImGui::IdGuard ig(&scroll_frame);
        int slider_scroll = scroll_frame;
        if (ImGui::SliderInt("##TimelineXSlider", &slider_scroll, 0, 1e6, "", ImGuiSliderFlags_NoInput)) {
            scroll_frame = slider_scroll;
        }
    }

    ImGui::EndChild();

    // === 5. Drag and drop

    if (hovered && ImGui::BeginDragDropTarget()) {
        // Проверяем, совпадает ли тип данных
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(POOL_DND)) {
            AudioSourcePtr data = *(AudioSourcePtr*)payload->Data;
            // Обработка полученных данных
            auto loc = mousePosToTrackAndFrame();

            ClipId_t clip_id =
                addClipToTimeline(Clip(data, loc.second), loc.first);

        }
        ImGui::EndDragDropTarget();
    }
}

} // namespace waves
