#include "common.h"

#include "timeline_view.h"

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

        draw_list->AddLine(prev_point, ImVec2(x, y), IM_COL32(255, 255, 255, 100), 2.f);
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
    float y_bottom = canvas_pos.y + track_height;

    ImVec2 start(x_start, y_top), end(x_end, y_bottom);

    // Цвета в зависимости от состояния
    ImU32 color_base = is_selected ? IM_COL32(100, 149, 237, 255)  // cornflower blue
                                   : IM_COL32(70, 130, 180, 200);   // steel blue
    ImU32 color_border = is_hovered ? IM_COL32(255, 255, 255, 255)
                                    : IM_COL32(255, 255, 255, 150);

    // Drawing rectangle over clip
    draw_list->AddRectFilled(start, end, color_base, 3.0f);
    draw_list->AddRect(start, end, color_border, 3.0f);

    // Drawing clip labe;
    std::string label = clip.name.empty() ? "Clip" : clip.name;
    draw_list->AddText(ImVec2(x_start + 4, y_top + 4), IM_COL32(255, 255, 255, 255), label.c_str());

    // Drawing waveform
    if (clip.source && (x_end - x_start) > 20) {
        DrawMiniWaveform(draw_list, clip,
                canvas_pos + ImVec2{0, ImGui::GetTextLineHeight()},
                track_height - ImGui::GetTextLineHeight(),
                {clip_left, clip_right});
    }
}

void TimelineView::DrawTimeGrid(ImDrawList *draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {

    float step = getBeatStepInPixels();

    auto [beat_idx, current_rel_x] = getNearestBeatInPixels();
    float y_start = canvas_pos.y;
    float y_end   = canvas_pos.y + canvas_size.y;

    while (current_rel_x < canvas_size.x) {
        float current_x = canvas_pos.x + current_rel_x;
        draw_list->AddLine(ImVec2{current_x, y_start}, ImVec2{current_x, y_end}, getGridLineCol());

        if (beat_idx % time_signature == 0) {
            std::string beat_str = std::to_string(beat_idx);
            draw_list->AddText(ImVec2{current_x, y_start}, getGridLineCol(), beat_str.c_str());
        }

        current_rel_x += step;
        beat_idx++;
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
        if (frame_delta < 0 && -frame_delta < clip->timeline_start_frame ) {
            clip->timeline_start_frame += frame_delta;
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

bool TimelineView::HandleVerticalClipDrag(TimeLine& timeline, ClipId_t clip_id, ImVec2 mouse_pos) {
    auto  current_track_idx = timeline.getTrackIdx(clip_id);
    if (!current_track_idx) return false;

    int expected_track_idx = (mouse_pos.y - canvas_pos.y) / track_height;

    if (expected_track_idx < 0 || expected_track_idx == *current_track_idx) return false;

    PLOG_DEBUG << "Moving clip " << clip_id << " from track " << *current_track_idx 
               << " to track " << expected_track_idx;

    timeline.moveClipToTrack(clip_id, expected_track_idx);
    return true;
}


/* ======================== DRAWING ======================================= */

void TimelineView::DrawTrack(Track& track) {

    bool modified = false;

    ImGui::PushID(&track);
    ImGui::BeginChild("Track_canvas", ImVec2(0, track_height), 0, ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 full_canvas_size = ImGui::GetContentRegionAvail();
    ImVec2 mouse_pos = ImGui::GetMousePos();

    // ===== Drawing clips =====
    for (Clip& clip: track.clips) {
        // Interaction handling
        HandleClipInteraction(clip, canvas_pos, mouse_pos);

        // Drawing
        bool is_hovered = (interaction.hovered_clip_id == clip.id);
        bool is_selected = (interaction.selected_clip_id == clip.id);
        DrawClip(draw_list, clip, canvas_pos, is_selected, is_hovered);

    }

    ImGui::EndChild();
    ImGui::PopID();
}

void TimelineView::DrawPlayHead(ImDrawList *draw_list, TimeLine& timeline, 
                                ImVec2 canvas_pos, ImVec2 size) {
    ImVec2 mouse_pos = ImGui::GetMousePos();

    ImRect timeline_rect = ImRect(canvas_pos, canvas_pos + size);
    if (timeline_rect.Contains(mouse_pos) && 
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {   
        timeline.playhead_frame.store(pixelToFrame(mouse_pos.x - canvas_pos.x));
    }

    ma_uint64 playhead_frame = timeline.playhead_frame.load();
    if (playhead_frame >= scroll_frame) {
        float playhead_x = canvas_pos.x + frameToPixel(playhead_frame);
        draw_list->AddLine(ImVec2(playhead_x, canvas_pos.y),
                          ImVec2(playhead_x, canvas_pos.y + size.y),
                          playhead_col, 2.0f);
    }   
}


void TimelineView::DrawTimeline(TimeLine& timeline) {
    bool modified = false;

    // Timeline over all available space
    ImGui::BeginChild("Timeline_canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 full_canvas_size = ImGui::GetContentRegionAvail();
    canvas_width = full_canvas_size.x;
    ImVec2 mouse_pos = ImGui::GetMousePos();

    // === 0. Resetting interaction

    interaction.has_changes = false;

    // === 1. Drawing time grid ===
    DrawTimeGrid(draw_list, canvas_pos, full_canvas_size);


    // === 2. Drawing tracks
    for (Track& track: timeline.tracks) {
        DrawTrack(track);
    }

    // === 3. Курсор воспроизведения ===
    DrawPlayHead(draw_list, timeline, canvas_pos, full_canvas_size);

    // === 4. Interaction ========================

    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // ~~ Mouse on empty space ~~
    if (!interaction.has_changes) {
        interaction.hovered_clip_id = CLIP_NONE;
        // click on empty space
        if (clicked) {
            interaction.selected_clip_id = CLIP_NONE;
        }
    }

    // ~~ Mouse released -> reset interaction ~~
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (interaction.mode != TimelineInteraction::Mode::None) {
            //TODO: add interaction to history
        }
        interaction.mode = TimelineInteraction::Mode::None;
        // PLOG_DEBUG << "Timeline interaction stop";
    }

    // Dragging handling
    if (interaction.selected_clip_id != CLIP_NONE && 
        interaction.mode == TimelineInteraction::Mode::DraggingClip &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) 
    {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (HandleHorizontalClipDrag(timeline, interaction.selected_clip_id, delta)) {
            modified = true;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        if (HandleVerticalClipDrag(timeline, interaction.selected_clip_id, mouse_pos)) {
            modified = true;
        }

    } 

    // Clip deletion
    if (interaction.selected_clip_id != CLIP_NONE &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) 
    {
        timeline.removeClipById(interaction.selected_clip_id);
        interaction.selected_clip_id = CLIP_NONE;
    }

    // === 5. Handling scroll and zoom ===
    bool keyCtrl_pressed = ImGui::GetIO().KeyCtrl;
    bool keyShift_pressed = ImGui::GetIO().KeyShift;
    float mouseWheel_delta = ImGui::GetIO().MouseWheel;

    if (ImGui::IsWindowHovered() && mouseWheel_delta != 0) {
        if (keyCtrl_pressed) {
            float mouse_x_rel = mouse_pos.x - canvas_pos.x;
            zoomAtPixel(mouse_x_rel, mouseWheel_delta > 0 ? 1.1f : 0.9f);
            modified = true;
        } else if (keyShift_pressed) {
            track_height *= mouseWheel_delta > 0 ? 1.1f : 0.9f;
        } else {
            float pixels_per_mouse_scroll = 50;
            float pixel_delta = (mouseWheel_delta > 0 ? 1.f: -1.f) * pixels_per_mouse_scroll;

            scrollByFrames(pixelToFrameRel(pixel_delta));

        }
    }


    ImGui::EndChild();
}

}
