#include "editor.h"

#include "ImGuiFileDialog.h"

#include "media_pool_view.h"

namespace waves {

void MediaPoolView::Draw(Editor& editor) {
    DrawSelectDialog(editor);
    DrawOpenedFiles(editor);
}

void MediaPoolView::DrawSelectDialog(Editor& editor) {
    if (ImGui::Button("Import audio")) {
        IGFD::FileDialogConfig config;
        config.path = "."; // starting from current directory
        config.countSelectionMax = 0; // selecting any number of files

        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".*,.wav,.mp3,.ogg", config);
    }
    // display
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
            std::map<std::string, std::string> selection =
                ImGuiFileDialog::Instance()->GetSelection();

            for (auto [name, path]: selection) {
                waves::AudioSourcePtr src = waves::decode_audio_from_file(name, path);

                editor.media_pool.push_back(src);

                //TODO:REMOVE
                editor.timeline.tracks[0].addClip(waves::Clip(src, 0));
            }
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }

}

void MediaPoolView::DrawOpenedFiles(Editor& editor) {
    int track_idx = 0;
    SourceIt eraseIt = editor.media_pool.end();

    for (auto it = editor.media_pool.begin(); it != editor.media_pool.end(); it++, track_idx++) {

        MediaPool &pool = editor.media_pool;
        const AudioSourcePtr src = *it;

        bool playing = editor.playback_state.isPlaying;
        SourceIt currentTrack = editor.playback_state.currentTrack;
        bool on_current = currentTrack == it;


        ImGui::PushID(track_idx);
            if (ImGui::Button("X")) {
                eraseIt = it;
            }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::Text("%s", src->name.c_str());

        if (!src->valid) {

            ImGui::SameLine();
            ImGui::Text("Failed to decode");

        } else {
            const char *button_text =
                (on_current && playing) ? "Stop" : "Play";

            ImGui::SameLine();
            ImGui::PushID(track_idx);
                if (ImGui::Button(button_text)) {
                    if (!on_current) {
                        editor.playback_state.setTrack(it);
                        editor.playback_state.setPlaying(true);
                    } else {
                        editor.playback_state.setPlaying(!playing);
                    }
                }
            ImGui::PopID();

            if (on_current) {
                // ImGui::SameLine();
                int slider_frame = editor.playback_state.getCurrentTrackPosInFrames();

                if (ImGui::SliderInt("Frame", &slider_frame, 0, editor.playback_state.getCurrentTrackLenInFrames())
                    && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    editor.playback_state.setCurrentTrackPosInFrames(slider_frame);
                }

            }

        }

    }

    if (eraseIt != editor.media_pool.end()) {
        if (editor.playback_state.currentTrack == eraseIt) {
            editor.playback_state.setPlaying(false);
            editor.playback_state.setTrack(editor.media_pool.end());
        }

        editor.media_pool.erase(eraseIt);
    }
}

} // namespace waves