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

    // applying window
    const std::vector<float> window = WindowFunction::generate(WindowFunction::Type::Hann, nfft);
    WindowFunction::applyInPlace<1>(window, cx_in.data(), cx_in.size());

    // applying fft
    kiss_fftr( cfg , cx_in.data() , cx_out.data() );

    auto clk_end = std::chrono::high_resolution_clock::now();

    analyze_time = (clk_end - clk_start) / 1.0s;
    PLOG_DEBUG << "Fftr took " << analyze_time;
    
    kiss_fft_free(cfg);


    // const size_t bin_count = std::min(1000ul, cx_out.size() );
    amps.resize(cx_out.size(), 0);

    for (uint32_t idx = 0; idx < cx_out.size(); idx++) {
        amps[idx ] += std::sqrt(cx_out[idx].r * cx_out[idx].r + cx_out[idx].i * cx_out[idx].i);
    }

    size_t max_idx = std::max_element(amps.begin(), amps.end()) - amps.begin();
    float max_amp = amps[max_idx];
    // normalizing values
    for (uint32_t idx = 0; idx < amps.size(); idx++) {
        amps[idx] = amps[idx] / max_amp;
    }

    max_freq = static_cast<float>(INNER_SAMPLE_RATE) / 2  * max_idx / amps.size();

    PLOG_DEBUG << "Max amp idx " << max_idx << "(val = " << max_amp << ") freq = " << max_freq; 

    open = true;
}

void FFT_Analyzer::analyzeBuffer() {
    if (!realtime_spectr_from_buffer || !buffer_) return;

    const std::vector<audio_sample_t> &data = buffer_->readerGetReadyBuffer();

    int nfft = (data.size() / INNER_CHANNELS) & (~1ull); // nfft must be even 
    if (cached_nfft != nfft) {
        cached_nfft = nfft;
        window = WindowFunction::generate(WindowFunction::Type::Hann, nfft);
        kiss_fftr_free(fft_cfg);
        fft_cfg = kiss_fftr_alloc(nfft, 0, NULL, NULL);
    }

    using namespace std::chrono_literals;
    auto clk_start = std::chrono::high_resolution_clock::now();

    temp_in.resize(nfft);
    temp_out.resize(nfft/2+1);
    
    for (uint32_t idx = 0; idx < nfft; idx++) {
        temp_in[idx] = (data[idx*2] + data[idx*2+1] ) / 2;
    }
    
    // applying window
    WindowFunction::applyInPlace<1>(window, temp_in.data(), temp_in.size());

    // applying fft
    kiss_fftr( fft_cfg , temp_in.data() , temp_out.data() );


    amps.resize(temp_out.size(), 0);
    for (uint32_t idx = 0; idx < amps.size(); idx++) {
        float r = temp_out[idx].r, i = temp_out[idx].i;
        amps[idx ] = std::sqrt(r * r + i * i);
    }

    size_t max_idx = std::max_element(amps.begin(), amps.end()) - amps.begin();

    max_freq = static_cast<float>(INNER_SAMPLE_RATE) / 2  * max_idx / amps.size();

    auto clk_end = std::chrono::high_resolution_clock::now();
    analyze_time = (clk_end - clk_start) / 1.0s;
    // PLOG_DEBUG << "Fftr took " << analyze_time;    


}


void FFT_Analyzer::DrawAnalyzed() {
    
    if (realtime_spectr_from_buffer ) {
        analyzeBuffer();
    }

    static float scale = 1.0f;
    static int cutoff_idx = amps.size();
    static int bins = 100;
    
    if (ImGui::Button("Reset")) {
        scale = 1.0f;
        cutoff_idx = amps.size();
        bins = 100;
    }

    ImGui::DragInt("Bins", &bins, 1, 10, amps.size());
    ImGui::DragFloat("Scale", &scale, 0.01, 0.01, 20);
    ImGui::DragInt("Cutoff idx", &cutoff_idx, 1, 10, amps.size());

    static std::vector<float> amps_bin;
    amps_bin.resize(bins);
    std::fill(amps_bin.begin(), amps_bin.end(), 0);

    for (int idx = 0; idx < cutoff_idx; idx++) {
        amps_bin[idx * bins / cutoff_idx] += amps[idx];
    }

    ImGui::PlotHistogram("##spectr2", amps_bin.data(), bins,
            0, NULL, 0.0f, 1/scale, ImVec2(0, 150.0f));
    
    ImGui::SeparatorText("Analyze Info");
    ImGui::Text("Main frequency: %.2f Hz", max_freq);
    ImGui::Text("Processing time: %.1f ms", analyze_time * 1000);

    if (realtime_spectr_from_buffer && ImGui::Button("Unsubscribe from buffer") ) {
        unsubscribe();
    }
}


}