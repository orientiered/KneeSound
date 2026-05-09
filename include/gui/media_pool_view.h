#pragma once


#include "core/playback_controller.h"

namespace waves {

class Editor;

struct MediaPoolView {
public:
    void Draw(Editor& editor);
private:
    void DrawSelectDialog(Editor& editor);
    void DrawOpenedFiles(PlaybackController& playback_state);
    void DrawFile(PlaybackController& playback_state, SourceIt it, int track_idx, bool &erase);
};

}
