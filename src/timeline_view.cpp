#include "common.h"

#include "timeline_view.h"
#include "imgui_misc.h"

#include "playback_state.h"

namespace waves {

//! Assuming that clip_timeline frames is visible
void TimelineView::DrawMiniWaveform(ImDrawList* draw_list, const waves::Clip& clip,
                      ImVec2 canvas_pos, float height, std::pair<ma_uint64, ma_uint64> timeline_clip_frames) {
    if (!clip.source || clip.source->pcmData.empty()) return;

    float center_y = canvas_pos.y + height / 2;

    ma_uint64 clip_start_frame = timeline_clip_frames.first - clip.timeline_start_frame + clip.source_start_frame;
    ma_uint64 clip_end_frame  = timeline_clip_frames.second - clip.timeline_start_frame + clip.source_start_frame;

    float start_x = canvas_pos.x + frameToPixel(timeline_clip_frames.first);
    float end_x   = canvas_pos.x + frameToPixel(timeline_clip_frames.second);
    float width   = end_x - start_x;

    // Calculating frame step
    // At least one frame or 1 pixel
    // int min_step = std::max(1.f, width / MAX_POINTS_PER_WAVEFORM);
    int step = std::max(1, static_cast<int>((clip_end_frame-clip_start_frame) / width));
    // generating waveform

    ImVec2 prev_point = ImVec2(start_x, center_y);

    //TODO: сделать так, чтобы вид вэйвформы не менялся при смещении
    for (ma_uint64 f = clip_start_frame; f < clip_end_frame; f += step) {
        float x = start_x + frameToPixelRel(f - clip_start_frame);
        // if (x > canvas_width) break;

        // Берём сэмпл (упрощённо: только первый канал, усреднение)
        float sample = clip.source->getMonoSampleAmplitude(f);
        float y = center_y - sample * (height / 2) * 0.9f; // 0.8 для отступа

        draw_list->AddLine(prev_point, ImVec2(x, y), col_waveform, 2.f);
        prev_point = ImVec2(x, y);
    }
}

void TimelineView::DrawClip(ImDrawList* draw_list, const Clip& clip,
              ImVec2 canvas_pos, bool is_selected, bool is_hovered) {

    // absolute frames on timeline
    auto [clip_left, clip_right] = getVisibleClipRange(clip);
    // clip is not visible, skipping
    if (clip_left >= clip_right) return;

    float x_start = canvas_pos.x + frameToPixel(clip_left);
    float x_end = canvas_pos.x + frameToPixel(clip_right);
    // TODO: draw two channels
    float y_top = canvas_pos.y + 0.f;
    float y_bottom = canvas_pos.y + track_height - track_pad;

    ImVec2 start(x_start, y_top), end(x_end, y_bottom);

    // Цвета в зависимости от состояния
    ImU32 color_base = is_selected ? col_clip_selected  
                                   : col_clip_base;   
    ImU32 color_border = is_hovered ? IM_COL32(255, 255, 255, 255)
                                    : IM_COL32(255, 255, 255, 150);

    // Drawing rectangle over clip
    draw_list->AddRectFilled(start, end, color_base, 3.0f);
    draw_list->AddRect(start, end, color_border, 3.0f);

    // Drawing clip label
    std::string label = clip.name.empty() ? "Clip" : clip.name;

    ImVec2 text_size = ImGui::CalcTextSize(label.c_str());

    if (text_size.x < (x_end - x_start))
        draw_list->AddText(start + ImVec2{4,4}, IM_COL32(255, 255, 255, 255), label.c_str());


    float text_height = ImGui::GetTextLineHeight();
    draw_list->AddLine(ImVec2{x_start,y_top + 4 + text_height},
                       ImVec2{x_end,  y_top + 4 + text_height}, color_border, 1);

    // Drawing waveform
    if (clip.source && (x_end - x_start) > 20) {
        DrawMiniWaveform(draw_list, clip,
                canvas_pos + ImVec2{0, text_height},
                track_height - text_height,
                {clip_left, clip_right});
    }
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

bool TimelineView::HandleClipInteraction(const Clip& clip,
                           ImVec2 canvas_pos, ImVec2 mouse_pos) {

    // absolute frames on timeline
    auto [clip_left, clip_right] = getVisibleClipRange(clip);
    // clip is not visible, skipping
    if (clip_left >= clip_right) return false;

    ImRect clip_rect = getClipRect(canvas_pos, track_height, clip_left, clip_right);
    bool hovered = clip_rect.Contains(mouse_pos);
    if (hovered) {
        interaction.hovered_clip_id = clip.id;
        interaction.has_changes = true;
    }
    
    // selecting clip on click
    //TODO: check click at the edge of the clip -> resize
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        PLOG_DEBUG << "Selected clip " << clip.id; 
        interaction.selected_clip_id = clip.id;
        interaction.mouse_start_pos = mouse_pos;
        interaction.drag_start_frame = clip.timeline_start_frame;
        interaction.mode = TimelineInteraction::Mode::DraggingClip;
        interaction.has_changes = true;
    }

    return false;
}

/* =================== DRAGGING ======================================== */

bool TimelineView::HandleHorizontalClipDrag(TimeLine& timeline, ClipId_t clip_id, ImVec2 mouse_delta) {
    // Конвертируем смещение в пикселях в кадры
    Clip* clip = timeline.getClipById(clip_id);
    if (!clip) return false;

    int64_t frame_delta = pixelToFrameRel(mouse_delta.x);

    if (frame_delta != 0) {
        PLOG_DEBUG << "Dragging clip " << clip_id << " to " << frame_delta << "frames";
        // Проверяем границы проекта
        if (frame_delta < 0) {
            clip->timeline_start_frame = std::max(0l, frame_delta + (int64_t)clip->timeline_start_frame);
            return true;
        } else if (frame_delta > 0) {
            clip->timeline_start_frame += frame_delta;
            // expanding timeline if necessary
            if (clip->getTimelineEndFrame() > total_frames) {
                total_frames = clip->getTimelineEndFrame();
            }

            return true;
        }
    }
    return false;
}

bool TimelineView::HandleVerticalClipDrag(TimeLine& timeline, ClipId_t clip_id) {
    auto  current_track_idx = timeline.getTrackIdx(clip_id);
    if (!current_track_idx) return false;


    int expected_track_idx = mousePosToTrackAndFrame().first;

    if (expected_track_idx < 0 || expected_track_idx == *current_track_idx) return false;

    PLOG_DEBUG << "Moving clip " << clip_id << " from track " << *current_track_idx 
               << " to track " << expected_track_idx;

    timeline.moveClipToTrack(clip_id, expected_track_idx);
    return true;
}


/* ======================== DRAWING ======================================= */

void TimelineView::DrawTrack(Track& track, bool parity) {

    ImGui::PushStyleColor(ImGuiCol_ChildBg, parity ? col_track_bg_even : col_track_bg_odd);

    // const float mult = 0.99;

    ImGui::PushID(&track);
    ImGui::BeginChild("Track_canvas", ImVec2(0, track_height), 0, ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 mouse_pos = ImGui::GetMousePos();

    /* ====== TRACK INFO AND CONTROLS ====================== */
    ImGui::BeginChild("Track info", ImVec2{track_info_width, track_height}, 0);

    // draw_list->AddRect(canvas_pos, canvas_pos + ImVec2{track_info_width, track_height - track_pad}, 
    //                 getGridLineCol());
    
    // track name
    ImGui::PushID(&track.name);
    ImGui::InputText("", &track.name);
    ImGui::PopID();
    // mute
    ImGui::PushID(&track.mute);
    ImGui::Checkbox("Mute", &track.mute);
    ImGui::PopID();

    // gain
    const float GAIN_MIN = -100;
    const float GAIN_MAX = +40;
    ImGui::PushID(&track.gain_db);
    ImGui::DragFloat("Gain", &track.gain_db, 0.3, GAIN_MIN, GAIN_MAX, "%.1f");
    ImGui::PopID();

    ImGui::EndChild();
    /* ========================================= */

    canvas_pos += ImVec2{track_info_width + track_pad, 0};

    // === Timeline part

    // ===== Drawing clips =====
    for (Clip& clip: track.clips) {
        // Interaction handling
        HandleClipInteraction(clip, canvas_pos, mouse_pos);

        // Drawing
        bool is_hovered = (interaction.hovered_clip_id == clip.id);
        bool is_selected = (interaction.selected_clip_id == clip.id);
        DrawClip(draw_list, clip, canvas_pos, is_selected, is_hovered);

    }

    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();

}

void TimelineView::DrawPlayHead(ImDrawList *draw_list, TimeLine& timeline, 
                                ImVec2 canvas_pos, ImVec2 size) {

    ma_uint64 playhead_frame = timeline.playhead_frame.load();
    if (playhead_frame >= scroll_frame) {
        float playhead_x = canvas_pos.x + frameToPixel(playhead_frame);
        draw_list->AddLine(ImVec2(playhead_x, canvas_pos.y),
                          ImVec2(playhead_x, canvas_pos.y + size.y),
                          col_playhead, 2.0f);
    }   
}



/* ========================== Interaction ================================ */

void TimelineView::HandleInteractions(PlaybackState& playback, TimeLine& timeline) {

    // 0 ~~ Mouse on empty space ~~
    if (!interaction.has_changes) {
        interaction.hovered_clip_id = CLIP_NONE;
        // click on empty space
        if (clicked) {
            interaction.selected_clip_id = CLIP_NONE;
        }
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
    if (clicked && interaction.selected_clip_id == CLIP_NONE) {
        timeline.playhead_frame.store(pixelToFrame(mouse_pos.x - field_pos.x));
    }

    // 3 Dragging handling
    if (interaction.selected_clip_id != CLIP_NONE && 
        interaction.mode == TimelineInteraction::Mode::DraggingClip &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) 
    {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (HandleHorizontalClipDrag(timeline, interaction.selected_clip_id, delta)) {
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        HandleVerticalClipDrag(timeline, interaction.selected_clip_id);

    } 

    // 4 Clip deletion
    if (interaction.selected_clip_id != CLIP_NONE &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) 
    {
        PLOG_DEBUG << "Deleting clip " << interaction.selected_clip_id;
        timeline.removeClipById(interaction.selected_clip_id);
        interaction.selected_clip_id = CLIP_NONE;
    }

    // 5 Clip cutting
    bool keyCtrl_pressed = ImGui::GetIO().KeyCtrl;
    bool keyShift_pressed = ImGui::GetIO().KeyShift;
    float mouseWheel_delta = ImGui::GetIO().MouseWheel;

    if (interaction.selected_clip_id != CLIP_NONE &&
        !keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_X)) {
        PLOG_DEBUG << "Cutting clip " << interaction.selected_clip_id;

        Clip *selected = timeline.getClipById(interaction.selected_clip_id);
        auto clip_loc = timeline.getTrackAndClipIdx(interaction.selected_clip_id);

        if (!selected) {
            PLOG_ERROR << "SELECTED CLIP " << interaction.selected_clip_id << " that is not present on timeline";
        }

        std::optional<Clip> new_clip = selected->cut(timeline.playhead_frame);
        if (new_clip) timeline.addClip(*new_clip, clip_loc->track_idx);
    }

    // 6 Clip copy-cut-pasting

    // Ctrl + C
    if (interaction.selected_clip_id != CLIP_NONE &&
        keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_C)) {
        PLOG_DEBUG << "Copying clip " << interaction.selected_clip_id;
        timeline.copyToClipboard(interaction.selected_clip_id);
    }

    // Ctrl + X
    if (interaction.selected_clip_id != CLIP_NONE &&
        keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_X)) {
        PLOG_DEBUG << "Copying clip " << interaction.selected_clip_id;
        timeline.cutToClipboard(interaction.selected_clip_id);
        interaction.selected_clip_id = CLIP_NONE;
    }

    // Ctrl + V

    if (keyCtrl_pressed && ImGui::IsKeyPressed(ImGuiKey_V) && hovered ) {
        PLOG_DEBUG << "Pasting clip";

        auto loc = mousePosToTrackAndFrame();

        std::optional<Clip> clip_opt = timeline.pasteFromClipboard();
        if (!clip_opt) return;

        clip_opt->timeline_start_frame = loc.second;
        timeline.addClip(*clip_opt, loc.first); 
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

void TimelineView::DrawTimeline(PlaybackState& playback, TimeLine& timeline) {

    // Timeline over all available space
    ImGui::BeginChild("Timeline_canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, 
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_HorizontalScrollbar);

    // === 0. Updating drawing state variables

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    canvas_pos = ImGui::GetCursorScreenPos();
    full_canvas_size = ImGui::GetContentRegionAvail();
    /*    track_info | timeline         */

    field_pos = canvas_pos + ImVec2{track_info_width, 0};
    field_size = full_canvas_size - ImVec2{track_info_width, 0};

    mouse_pos = ImGui::GetMousePos();
    // mouse is in timeline zone
    hovered_all = ImRect(canvas_pos, canvas_pos + full_canvas_size).Contains(mouse_pos);
    hovered = ImRect(field_pos, field_pos + field_size).Contains(mouse_pos);
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // Resetting interaction
    interaction.has_changes = false;

    // === 1. Drawing tracks
    ImGui::SetCursorScreenPos(canvas_pos + ImVec2{0,grid_line_header});
    for (int idx = 0; idx < timeline.tracks.size(); idx++) {
        DrawTrack(timeline.tracks[idx], idx%2);

    }

    ImGui::SetCursorScreenPos(canvas_pos);
    // === 2. Drawing time grid ===
    DrawTimeGrid(draw_list, field_pos, field_size);
    

    // === 3. Курсор воспроизведения ===
    DrawPlayHead(draw_list, timeline, field_pos, field_size);

    // === 4. Interaction ========================

    HandleInteractions(playback, timeline);


    ImGui::EndChild();

    // === 5. Drag and drop

    if (hovered && ImGui::BeginDragDropTarget()) {
        // Проверяем, совпадает ли тип данных
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(POOL_DND)) {
            AudioSourcePtr data = *(AudioSourcePtr*)payload->Data;
            // Обработка полученных данных
            auto loc = mousePosToTrackAndFrame();

            ClipId_t clip_id = timeline.addClip(Clip(data, loc.second), loc.first);

            // expanding timeline if necessary
            Clip *clip = timeline.getClipById(clip_id);
            if (clip->getTimelineEndFrame() > total_frames) {
                total_frames = clip->getTimelineEndFrame();
            }
        }
        ImGui::EndDragDropTarget();
    }
}

}
