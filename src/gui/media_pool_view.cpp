#include "core/audio_source.h"
#include "core/playback_controller.h"
#include "core/timeline.h"
#include "editor.h"

#include "ImGuiFileDialog.h"

#include "gui/media_pool_view.h"

#include "imgui_misc.h"
#include <imgui.h>

namespace waves {

void MediaPoolView::Draw(Editor& editor) {
    DrawSelectDialog(editor.media_pool);
    DrawOpenedFiles(editor.playback_state);
}

void MediaPoolView::DrawSelectDialog(MediaPool &media_pool) {
    const char * IMPORT_DLG_KEY = "ChooseImportAudioKey";
    if (ImGui::Button("Import audio")) {
        IGFD::FileDialogConfig config;
        config.path = "."; // starting from current directory
        config.countSelectionMax = 0; // selecting any number of files

        ImGuiFileDialog::Instance()->OpenDialog(IMPORT_DLG_KEY, "Choose File",
             "Audio files (*.wav *.mp3 *.flac){.wav,.mp3,.flac}, All{.*}", config);
    }
    // display
    if (ImGuiFileDialog::Instance()->Display(IMPORT_DLG_KEY)) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
            std::map<std::string, std::string> selection =
                ImGuiFileDialog::Instance()->GetSelection();

            for (auto [name, path]: selection) {
                AudioSourcePtr src = decode_audio_from_file(name, path);

                media_pool.push_back(src);
            }
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }

}

bool MediaPoolView::DrawFile(MediaPool &media_pool, int track_idx, bool &erase) {
    const AudioSourcePtr src = media_pool.getTrackList()[track_idx];

    bool playing = media_pool.getPlaying();
    AudioSourcePtr currentTrack = media_pool.getCurrentTrack();
    bool on_current = src == currentTrack;

    bool needs_focus = false;

    ID_GUARD(track_idx,
        if (ImGui::Button("X")) {
            erase = true;
        }
    );

    ImGui::SameLine();
    if (src->loading) {
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(0.0f, 0.0f), src->name.c_str());
    } else {
        ImGui::Text("%s", src->name.c_str());
    }
    // drag and drop
    if (src->valid && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID) ) {

        AudioSourcePtr data = src; // Sending audio source
        ImGui::SetDragDropPayload(POOL_DND, &data, sizeof(AudioSourcePtr));

        // Displaying name of the payload
        ImGui::Text("Clip %s", data->name.c_str());
        ImGui::EndDragDropSource();
    }

    if (src->loading) {
        // do nothing
    } else if (!src->valid) {
        ImGui::SameLine();
        ImGui::Text("Failed to decode");

    } else {
        const char *button_text =
            (on_current && playing) ? "Stop" : "Play";
        bool play_state = on_current && playing;

        ImGui::SameLine();
        ImVec2 widget_size(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing());
        ID_GUARD(track_idx,
            if (ImGui::PlayStopWidget(widget_size, play_state)) {
                needs_focus = true;
                if (!on_current) {
                    media_pool.setTrack(src);
                    media_pool.setPlaying(true);
                } else {
                    media_pool.setPlaying(!playing);
                }

            }
        );

        if (on_current) {

            int slider_frame = media_pool.getCurrentFrame();
            float time_sec = frameToSec(slider_frame);
            float end_sec = frameToSec(media_pool.getCurrentTrackLenInFrames());

            bool slider_change = ImGui::SliderFloat("##Frame", &time_sec, 0, end_sec, "%.2f s");

            if (slider_change && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || std::abs(ImGui::GetMouseDragDelta().x) > 0)) {
                ImGui::ResetMouseDragDelta();
                media_pool.setCurrentTrackPosInFrames(secToFrame(time_sec));
            }

        }

    }

    return needs_focus;
}

void MediaPoolView::DrawOpenedFiles(PlaybackController& playback_state) {
    MediaPool& media_pool = playback_state.pool;

    const std::vector<AudioSourcePtr> src_list = media_pool.getTrackList();
    int erase_idx = -1;

    for (int track_idx = 0; track_idx < src_list.size(); track_idx++) {
        bool erase = false;
        bool need_focus = DrawFile(media_pool, track_idx, erase);
        if (erase)
            erase_idx = track_idx;

        if (need_focus) {
            playback_state.setPoolSrc();
        }

    }

    if (erase_idx >= 0) {
        AudioSourcePtr erase_src = src_list[erase_idx];
        media_pool.erase(erase_src);

        PLOG_INFO << "Removed source " << erase_src->name << " from media pool";
    }
}

} // namespace waves
