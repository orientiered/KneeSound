#pragma once

#include "audio_effects.h"

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

class FFT_EqualizerView : public IEffectView {
private:
    enum Preset {
        NONE = -1,
        LOWPASS = 0,
        HIGHPASS,
        BANDPASS,
        REJECTOR,
        KBAND
    };

    Preset preset = NONE;
    Preset applied_preset = NONE;

    // lowpass
    struct Lowpass {
        float cutoff;
        float attenuation;
    } lowpass;
    // highpass
    struct Highpass {
        float cutoff = 1000;
        float attenuation = 10;
    } highpass;
    // bandpass
    struct Bandpass {
        float left_cutoff = 1000;
        float right_cutoff = 2000;
        float left_attenuation = 10;
        float right_attenuation = 10;
    } bandpass;
    // rejector
    struct Rejector {
        float left_cutoff = 40;
        float right_cutoff = 60;
        float left_attenuation = 20;
        float right_attenuation = 20;
        // float gain_db = -30; // gain in rejection band
    } rejector;
    // k-band
    struct KBand {
        struct Band {
            float freq_log;
            float gain_db;
        };
        std::vector<Band> bands;
        int band_count = 5;
    } kband;

    const float MAX_FREQ = static_cast<float>(INNER_SAMPLE_RATE) / 2;
    const float MIN_FREQ = 20.0f;
    float logFreqToNormal(float freq_log) {
        return MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, freq_log);
    }
    float freqToLog(float freq) {
        return std::log(freq / MIN_FREQ) / std::log(MAX_FREQ / MIN_FREQ);
    }

    // Set new preset, true if changed
    bool setPreset(Preset preset_) {
        bool result = preset_ != preset;
        preset = preset_;
        return result;
    }
public:
    std::vector<float> frequency_response;
    void DrawLowpass();
    void DrawHighpass();
    void DrawBandpass();
    void DrawRejector();
    void DrawKBand();

    void setResponseSize(size_t size);
    void saveAppliedPreset() { applied_preset = preset; }

    std::vector<float> &calculateLowpass();
    std::vector<float> &calculateHighpass();
    std::vector<float> &calculateBandpass();
    std::vector<float> &calculateRejector();
    std::vector<float> &calculateKBand();

    void DrawSettings() override;
    FFT_EqualizerView(FFT_Equalizer *eq_): eq(eq_) {}
private:
    FFT_Equalizer *eq;
};


}
