#pragma once

#include "common.h"
#include "effects/audio_effects.h"
#include "core/fft_utils.h"


namespace waves {

class FFT_Analyzer : public IDspKernel {
private:
    std::mutex mtx_;

public:
    ReadableStreamingBuffer buffer_;
    void process(const AudioBuffer& in, AudioBuffer &out) override;
    uint32_t getLatencySamples() const override { return 0; }
    void reset() override {};

    FFT_Analyzer(): buffer_(mtx_, RENDER_BLOCK_SIZE, INNER_CHANNELS) {}

    ~FFT_Analyzer() override = default;
};

class FFT_AnalyzerView : public IEffectView {
private:
    // runtime info
    bool open = false;
    std::vector<float> amps;
    std::vector<float> log_amps;

    float analyze_time = 0;
    float max_freq = 0;

    // for realtime analyze
    WindowedKissFFTR wfftr;

    std::vector<float> temp_in;
    std::vector<kiss_fft_cpx> temp_out;
    void analyzeBuffer();

    // drawing and analysis preferences
    WindowFunction::Type window_type = WindowFunction::Type::Hann;
    int window_idx = 1;
    float scale = 1.0f;
    int cutoff_idx = 100;
    int bins = 100;
    
public:
    ~FFT_AnalyzerView() override = default;
    FFT_AnalyzerView(FFT_Analyzer *analyzer) : analyzer_(analyzer) {}

    void serialize(ProjectWriter output) const override;
    void deserialize(ProjectReader input) override;

    void DrawSettings() override;
private:
    FFT_Analyzer *analyzer_;
};

class FFT_AnalyzerFactory : public IEffectFactory {
public:
    ~FFT_AnalyzerFactory() override = default;
    EffectDescriptor getDescriptor() override {
        return {
            .name = "FFT analyzer",
            .version = "1.0",
            .id = "knee_fft_analyzer"
        };
    }

    PluginPair build() override {
        std::unique_ptr<FFT_Analyzer> kernel = std::make_unique<FFT_Analyzer>();
        std::unique_ptr<IEffectView> view = std::make_unique<FFT_AnalyzerView>(kernel.get());

        return {std::move(kernel), std::move(view)};
    }
};

}
