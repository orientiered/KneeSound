#pragma once

#include "common.h"

#include "kiss_fftr.h"
#include "timeline.h"

namespace waves {

class FFT_Analyzer {
    std::vector<float> amps;

    float analyze_time = 0;
    float max_freq = 0;

    bool realtime_spectr_from_buffer = false;
    // for realtime analyze
    ReadableStreamingBuffer *buffer_ = nullptr; 
    int cached_nfft = -1;
    kiss_fftr_cfg fft_cfg = nullptr;
    std::vector<float> window;
    std::vector<float> temp_in;
    std::vector<kiss_fft_cpx> temp_out;

public:
    bool open = false;

    // Run FFT on clip one time
    void analyzeClip(const Clip &clip);
    void subscribeToBuffer(ReadableStreamingBuffer *rsbuffer) {
        realtime_spectr_from_buffer = true;
        buffer_ = rsbuffer;
        open = true;
    }
    void unsubscribe() { realtime_spectr_from_buffer = false; buffer_ = nullptr; }
    // Run FFT analyzis on buffer evry frame
    void analyzeBuffer();
    
    void DrawAnalyzed();    


    ~FFT_Analyzer() {
        kiss_fftr_free(fft_cfg);
    }
};


}