#pragma once

#include "common.h"


namespace waves {

class Editor;

struct MediaPoolView {
public:
    void Draw(Editor& editor);
private:
    void DrawSelectDialog(Editor& editor); 
    void DrawOpenedFiles(Editor& editor);
};

}

