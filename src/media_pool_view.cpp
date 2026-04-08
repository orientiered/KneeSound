#include "editor.h"

#include "ImGuiFileDialog.h"

#include "media_pool_view.h"

namespace waves {

void MediaPoolView::Draw(Editor& editor) {
    DrawSelectDialog(editor);
    DrawOpenedFiles(editor.playback_state);
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

            }
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }

}


void MediaPoolView::DrawFile(PlaybackState& playback_state, SourceIt it, int track_idx, bool &erase) {
    MediaPool &pool = playback_state.pool;
    const AudioSourcePtr src = *it;

    bool playing = playback_state.isPlaying;
    SourceIt currentTrack = playback_state.currentTrack;
    bool on_current = currentTrack == it;


    ImGui::PushID(track_idx);
        if (ImGui::Button("X")) {
            erase = true;
        }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::Text("%s", src->name.c_str());
    // drag and drop
    if (src->valid && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID) ) {

        AudioSourcePtr data = *it; // Sending audio source 
        ImGui::SetDragDropPayload(POOL_DND, &data, sizeof(AudioSourcePtr));
        
        // Displaying name of the payload
        ImGui::Text("Clip %s", data->name.c_str()); 
        ImGui::EndDragDropSource();
    }

    if (!src->valid) {

        ImGui::SameLine();
        ImGui::Text("Failed to decode");

    } else {
        const char *button_text =
            (on_current && playing) ? "Stop" : "Play";

        ImGui::SameLine();
        ImGui::PushID(track_idx);
            if (ImGui::Button(button_text)) {
                //TODO: refactor
                playback_state.src = POOL_SRC;
                if (!on_current) {
                    playback_state.setTrack(it);
                    playback_state.setPlaying(true);
                } else {
                    playback_state.setPlaying(!playing);
                }
            }
        ImGui::PopID();

        if (on_current) {
            // ImGui::SameLine();
            int slider_frame = playback_state.getCurrentTrackPosInFrames();

            if (ImGui::SliderInt("Frame", &slider_frame, 0, playback_state.getCurrentTrackLenInFrames())
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                playback_state.setCurrentTrackPosInFrames(slider_frame);
            }

        }

    }
}

void MediaPoolView::DrawOpenedFiles(PlaybackState& playback_state) {
    int track_idx = 0;
    MediaPool& media_pool = playback_state.pool;
    SourceIt eraseIt = media_pool.end();

    for (auto it = media_pool.begin(); it != media_pool.end(); it++, track_idx++) {
        bool erase = false;
        DrawFile(playback_state, it, track_idx, erase);
        if (erase) 
            eraseIt = it;
        
    }

    if (eraseIt != media_pool.end()) {
        if (playback_state.currentTrack == eraseIt) {
            playback_state.setPlaying(false);
            playback_state.setTrack(media_pool.end());
        }

        PLOG_INFO << "Removed source " << eraseIt->get()->name << " from media pool";
        media_pool.erase(eraseIt);
    }
}

} // namespace waves