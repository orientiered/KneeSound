#pragma once

#include "effects/audio_effects.h"
#include "common.h"
#include <variant>

namespace waves {

/* ========================== EQUALIZER ======================== */
class FFT_Equalizer: public IDspKernel {
private:
    FreqDomainPipeline fft_pipeline_;
    std::vector<float> freq_response_;      ///< Response curve used in processing
    std::vector<float> new_freq_response_;  ///< Used for asynchronous response change
    bool freq_response_updated_ = true;
public:
    FFT_Equalizer(size_t block_size):
        fft_pipeline_(block_size, INNER_CHANNELS),
        freq_response_(block_size + 1, 1.0f) {}

    void reset() override {
        fft_pipeline_.reset();
    }

    uint32_t getLatencySamples() const override {
        return fft_pipeline_.getLatency();
    }

    void process(const AudioBuffer &in, AudioBuffer &out) override {
        fft_pipeline_.processBlock(in, out, *this);
    }

    size_t getSize() const noexcept { return freq_response_.size(); }

    void setFreqResponse(const std::vector<float> response) {
        if (response.size() != freq_response_.size())
            throw std::invalid_argument("Frequency response size must be block_size + 1");

        new_freq_response_ = response;
        freq_response_updated_ = false;
    }

    void processFreqData(std::vector<kiss_fft_cpx> &freq_data)  {
        // Updating frequency response curve
        //TODO: potential race condition, but extremely rare
        if (!freq_response_updated_) {
            std::swap(freq_response_, new_freq_response_);
            freq_response_updated_ = true;
        }

        const size_t spectr_size = freq_response_.size();
        for (size_t i = 0; i < spectr_size; i++) {
            freq_data[i].i *= freq_response_[i];
            freq_data[i].r *= freq_response_[i];
        }
    }

    ~FFT_Equalizer() override = default;
};


namespace fft_detail {
    const float MAX_FREQ = static_cast<float>(INNER_SAMPLE_RATE) / 2;
    const float MIN_FREQ = 20.0f;
}

class FFT_Lowpass {
public:
    std::string name = "Lowpass";

    float cutoff = 1000;
    float attenuation = 10;

    bool Draw();
    void calculate(std::vector<float> &response);
};

class FFT_Highpass {
public:
    std::string name = "Highpass";

    float cutoff = 1000;
    float attenuation = 10;

    bool Draw();
    void calculate(std::vector<float> &response);
};

class FFT_Bandpass {
public:
    std::string name = "Bandpass";

    float left_cutoff = 1000;
    float right_cutoff = 2000;
    float left_attenuation = 10;
    float right_attenuation = 10;

    bool Draw();
    void calculate(std::vector<float> &response);
};

class FFT_Rejector {
public:
    std::string name = "Rejector";

    float left_cutoff = 40;
    float right_cutoff = 60;
    float left_attenuation = 20;
    float right_attenuation = 20;

    bool Draw();
    void calculate(std::vector<float> &response);
};

class FFT_KBand {
public:
    std::string name = "K-band";

    struct Band {
        float freq_log;
        float gain_db;
    };
    std::vector<Band> bands;
    int band_count = 5;

    bool Draw();
    void calculate(std::vector<float> &response);
};


using PresetClass = std::variant<
    FFT_Lowpass,
    FFT_Highpass,
    FFT_Bandpass,
    FFT_Rejector,
    FFT_KBand
>;

inline bool PresetDraw(PresetClass& preset) {
    auto draw_visitor = [] (auto &preset) {
        return preset.Draw();
    };
    return std::visit(draw_visitor, preset);
}

inline void PresetCalculate(PresetClass& preset, std::vector<float> &response) {
    auto calc_visitor = [&response] (auto &preset) {
        return preset.calculate(response);
    };
    return std::visit(calc_visitor, preset);
}

inline std::string PresetName(PresetClass& preset) {
    auto name_visitor = [] (auto &preset) {
        return preset.name;
    };
    return std::visit(name_visitor, preset);
}

class FFT_EqualizerView : public IEffectView {
private:

    using Preset = int;

    Preset preset = 0;
    Preset applied_preset = 0;

    std::vector<PresetClass> presets = {
        FFT_Lowpass(),
        FFT_Highpass(),
        FFT_Bandpass(),
        FFT_Rejector(),
        FFT_KBand()
    };

    // Set new preset, true if changed
    bool setPreset(Preset preset_) {
        bool result = preset_ != preset;
        preset = preset_;
        return result;
    }
public:
    std::vector<float> frequency_response;
    std::vector<float> log_freq_response;
    bool useLogResponse = true;

    void setResponseSize(size_t size);
    void saveAppliedPreset() { applied_preset = preset; }

    void DrawSettings() override;
    FFT_EqualizerView(FFT_Equalizer *eq_): eq(eq_) {}
private:
    FFT_Equalizer *eq;
};

class FFT_EqualizerFactory: public IEffectFactory {
public:
    ~FFT_EqualizerFactory() override = default;
    EffectDescriptor getDescriptor() override {
        return {
            .name = "FFT equalizer",
            .version = "1.0",
            .id = "knee_fft_equalizer"
        };
    }

    PluginPair build() override {
        // TODO fix hardcoded block size
        std::unique_ptr<FFT_Equalizer> kernel = std::make_unique<FFT_Equalizer>(RENDER_BLOCK_SIZE);
        std::unique_ptr<IEffectView> view = std::make_unique<FFT_EqualizerView>(kernel.get());

        return {std::move(kernel), std::move(view)};
    }

};


}
