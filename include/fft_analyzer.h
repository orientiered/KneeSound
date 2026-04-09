#pragma once

#include "common.h"

#include "timeline.h"

namespace waves {

class FFT_Analyzer {
    std::vector<float> amps;

    float analyze_time = 0;
    float max_freq = 0;
public:
    bool open = false;

    void analyzeClip(const Clip &clip);

    void DrawAnalyzed();    

};


}