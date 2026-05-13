#pragma once


#include "core/playback_controller.h"

namespace waves {

class Editor;

struct MediaPoolView {
public:
    void Draw(Editor& editor);
private:
    void DrawSelectDialog(MediaPool &media_pool);
    void DrawOpenedFiles(PlaybackController& playback_state);
    // @return True if play requested
    bool DrawFile(MediaPool &media_pool, int track_idx, bool &erase);
};

}
