#include "fft_analyzer.h"

#include "kiss_fftr.h"
#include "imgui.h"
#include <algorithm>

namespace waves {

void FFT_Analyzer::analyzeClip(const Clip &clip) {

    size_t nfft = std::min(1000000ull, clip.getDurationFrames()) & (~1ll);

    auto nextPowerOfTwo = [](size_t n) {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    };

    nfft = nextPowerOfTwo(nfft) / 2;


    PLOG_DEBUG << "Analyzing " << nfft << " frames with fft";

    kiss_fftr_cfg cfg = kiss_fftr_alloc(nfft, 0 ,0,0 );
    
    std::vector<float> cx_in(nfft);
    std::vector<kiss_fft_cpx> cx_out(nfft / 2 + 1);

    for (uint32_t idx = 0; idx < nfft; idx++) {
        uint32_t frame = idx + clip.source_start_frame;
        cx_in[idx] = clip.source->getMonoSampleAmplitude(frame);
    }
    
    using namespace std::chrono_literals;

    auto clk_start = std::chrono::high_resolution_clock::now();

    kiss_fftr( cfg , cx_in.data() , cx_out.data() );

    auto clk_end = std::chrono::high_resolution_clock::now();

    analyze_time = (clk_end - clk_start) / 1.0s;
    PLOG_DEBUG << "Fftr took " << analyze_time;
    

    amps.resize(cx_out.size());

    for (uint32_t idx = 0; idx < amps.size(); idx++) {
        amps[idx] = std::sqrt(cx_out[idx].r * cx_out[idx].r + cx_out[idx].i * cx_out[idx].i);
    }

    kiss_fft_free(cfg);

    size_t max_idx = std::max_element(amps.begin(), amps.end()) - amps.begin();
    max_freq = static_cast<float>(INNER_SAMPLE_RATE) / 2  * max_idx / amps.size();

    PLOG_DEBUG << "Max amp idx " << max_idx << " freq = " << max_freq; 

    open = true;
}


void FFT_Analyzer::DrawAnalyzed() {
    ImGui::PlotHistogram("##spectr", amps.data(), amps.size()/2, 
            0, NULL, 0.0f, 10.0f, ImVec2(0, 150.0f));
    
    ImGui::SeparatorText("Analyze Info");
    ImGui::Text("Main frequency: %.2f Hz", max_freq);
    ImGui::Text("Processing time: %.1f ms", analyze_time * 1000);
}


}