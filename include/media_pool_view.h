#pragma once

#include "common.h"

#include "playback_state.h"

namespace waves {

class Editor;

struct MediaPoolView {
public:
    void Draw(Editor& editor);
private:
    void DrawSelectDialog(Editor& editor); 
    void DrawOpenedFiles(PlaybackState& playback_state);
    void DrawFile(PlaybackState& playback_state, SourceIt it, int track_idx, bool &erase);
};

}

