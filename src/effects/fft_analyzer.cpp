
#include "buffer_utils.h"
#include "common.h"
#include "effects/fft_analyzer.h"
#include "fft_utils.h"
#include "imgui.h"
#include "misc_utils.h"
#include <algorithm>

namespace waves {

void FFT_Analyzer::process(const AudioBuffer& in, AudioBuffer &out) {
    if (in != out) {
        // just copy data
        for (size_t ch = 0; ch < out.getChannels(); ch++) {
            std::copy_n(in[ch], out.getFrameCount(), out.getChannel(ch));
        }
    }

    AudioBuffer &buf = buffer_.writerGetBuffer(in.getFrameCount(), in.getChannels());
    // copying to buffer that is accesible by gui thread
    for (size_t ch = 0; ch < in.getChannels(); ch++) {
        std::copy_n(in[ch], in.getFrameCount(), buf.getChannel(ch));
    }
    buffer_.writerSentReadyBuffer();

}

void FFT_AnalyzerView::analyzeBuffer() {
    if (!analyzer_) return;

    const AudioBuffer &buf = analyzer_->buffer_.readerGetReadyBuffer();
    const audio_sample_t *data = buf[0];

    int nfft = buf.getFrameCount() & (~1ull); // nfft must be even

    if (wfftr.getNfft() != nfft) {
        wfftr = WindowedKissFFTR(nfft, window_type, true);
    } else if (wfftr.getType() != window_type) {
        wfftr.recalcWindow(window_type);
    }

    using namespace std::chrono_literals;
    auto clk_start = std::chrono::high_resolution_clock::now();

    temp_in.resize(nfft);
    temp_out.resize(nfft/2+1);

    for (uint32_t idx = 0; idx < nfft; idx++) {
        temp_in[idx] = (data[idx*2] + data[idx*2+1] ) / 2;
    }


    // applying window and fft
    wfftr.forward(temp_in.data() , temp_out.data() );

    log_amps = convertToDoubleLogScale(temp_out, INNER_SAMPLE_RATE, nfft);

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

void FFT_AnalyzerView::DrawSettings() {
    analyzeBuffer();

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

    static int window_idx = 1;
    const int window_count = 4;
    const char * const window_labels[] = {
        "Rectangle [NONE]",
        "Hann",
        "Hamming",
        "Blackman"
    };
    using wType = WindowFunction::Type;
    const wType window_types[] = {
        wType::Rectangle,
        wType::Hann,
        wType::Hamming,
        wType::Blackman
    };

    if (ImGui::ListBox("Window type", &window_idx, window_labels,
        window_count)) {
        window_type = window_types[window_idx];
    }

    ImGui::PlotHistogram("##spectr3", log_amps.data(), log_amps.size(),
            0, NULL, 0.0f, 1/scale, ImVec2(0, 150.0f));

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

}


}
