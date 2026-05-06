#pragma once

#include "common.h"

#include "kiss_fftr.h"
#include "fft_utils.h"
#include "algorithm"
#include <cmath>
#include <functional>

namespace waves {

/* ================ GENERAL CLASS FOR EFFECT IN FREQUENCY DOMAIN ========== */
/// 0. For each channel:
/// 1. Concat data[block_size] with previous segment
/// 2. Perform real fft with Hann window -> freq_data[block_size+1]
/// 3. Apply given effect with 'Processor' callable on freq_data
/// 4. Inverse fft with normalization
/// 5. Overlapp add with previous calculated segment
/// Important: due to overlapp-add has latency of 1 block size
class FreqDomainEffect {
private:
    WindowedKissFFTR wfftr_;

    size_t hop_size_;
    size_t channels_;

    // overlap and previous are stored sequentially, not interleaved
    std::vector<audio_sample_t> overlap_add_;
    std::vector<audio_sample_t> previous_block_;

    std::vector<audio_sample_t> time_data; ///< array for temporary calculations
    std::vector<kiss_fft_cpx>   freq_data; ///< array for temporary calculations

public:
    FreqDomainEffect(size_t block_size, size_t channels):
        wfftr_(2*block_size, WindowFunction::Type::Hann, true, true),
        hop_size_(block_size), channels_(channels),
        overlap_add_(hop_size_ * channels_, 0.0f),
        previous_block_(hop_size_ * channels_, 0.0f),
        time_data(2*block_size, 0.0f),
        freq_data(block_size + 1) {}

    size_t getLatency() const noexcept { return hop_size_; } ///< Latency in samples
    size_t getBlockSize() const noexcept { return hop_size_; }
    size_t getFreqDataSize() const noexcept { return hop_size_ + 1; }

    void reset() {
        std::fill(overlap_add_.begin(), overlap_add_.end(), 0.0f);
        std::fill(previous_block_.begin(), previous_block_.end(), 0.0f);
    }

    template<typename Processor>
    void processBlock(audio_sample_t *inout, Processor &processor) {
        for (size_t ch_idx = 0; ch_idx < channels_; ch_idx++) {

            prepareTimeData(inout, ch_idx);

            wfftr_.forward(time_data.data(), freq_data.data());

            processor(freq_data);

            wfftr_.inverse(freq_data.data(), time_data.data());
            wfftr_.normalize(time_data.data());

            applyOverlap(inout, ch_idx);
        }

    }

private:
    void prepareTimeData(audio_sample_t *in, size_t ch_idx);
    void applyOverlap(audio_sample_t *out, size_t ch_idx);
};

/* ========================== Interface for freq domain effect ========== */

// using FreqEffectCallback_t =
class IFreqEffect {
public:
    virtual void operator()(std::vector<kiss_fft_cpx> &freq_data) = 0;
    virtual ~IFreqEffect() = default;
};

/* ========================== EQUALIZER ======================== */
class Equalizer: IFreqEffect {
private:
    std::vector<float> freq_response_;      ///< Response curve used in processing
    std::vector<float> new_freq_response_;  ///< Used for asynchronous response change
    bool freq_response_updated_ = true;
public:
    Equalizer(size_t block_size):
        freq_response_(block_size + 1, 1.0f) {}

    size_t getSize() const noexcept { return freq_response_.size(); }

    void setFreqResponse(const std::vector<float> response) {
        if (response.size() != freq_response_.size())
            throw std::invalid_argument("Frequency response size must be block_size + 1");

        new_freq_response_ = response;
        freq_response_updated_ = false;
    }

    void operator()(std::vector<kiss_fft_cpx> &freq_data) override {
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

    ~Equalizer() override = default;
};

class ITimeEffect {
public:
    virtual void operator()(const audio_sample_t* input, audio_sample_t* output, size_t numSamples) = 0;
    virtual ~ITimeEffect() = default;

};

class BiquadFilter : public ITimeEffect {
private:
    // Filter coeffs
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // Filter state
    float s1 = 0.0f, s2 = 0.0f;
public:
    void setCoefficients(float f1, float f2, float f3, float g1, float g2) {
        b0 = f1; b1 = f2; b2 = f3;
        a1 = g1; a2 = g2;
    }

    // One sample processing, Transposed direct form 2
    inline float process(float x) {
        float y = b0 * x + s1;
        s1 = b1 * x + s2 - a1 * y;
        s2 = b2 * x - a2 * y;
        return y;
    }

    ~BiquadFilter() override = default;
    virtual void operator()(const audio_sample_t* input, audio_sample_t* output, size_t numSamples) override {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }
};


struct BiquadSettings {
    struct Coeffs {
        float f1, f2, f3; // numerator
        float g1, g2;     // denumerator (with a0=1, normalized form)

        Coeffs(float b1, float b2, float b3, float a1, float a2):
            f1(b1), f2(b2), f3(b3), g1(a1), g2(a2) {}
        // With normalization
        Coeffs(double b1, double b2, double b3, double a0, double a1, double a2):
            f1(b1/a0), f2(b2/a0), f3(b3/a0), g1(a1/a0), g2(a2/a0) {}
    };

private:
    // fc = центральная частота, Q = добротность, gainDb = усиление в дБ
    // fs = частота дискретизации
    // src: https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
    struct RBJ_Params {
        double A;
        double w0;
        double cos_w0, sin_w0;
        double alpha;

        RBJ_Params(double fc, double Q, double gainDb, double fs) {
            A  = std::pow(10.0, gainDb / 40.0);
            w0 = 2.0 * M_PI * fc / fs;
            cos_w0 = std::cos(w0);
            sin_w0 = std::sin(w0);
            alpha  = sin_w0 / (2.0 * Q);
        }
    };

public:
    Coeffs makeLPF(double fc, double Q, double gainDb, double fs);

    Coeffs makeHPF(double fc, double Q, double gainDb, double fs);

    Coeffs makeBPF(double fc, double Q, double gainDb, double fs);

    Coeffs makeNotch(double fc, double Q, double gainDb, double fs);

    Coeffs makePeaking(double fc, double Q, double gainDb, double fs);

    std::vector<float> frequency_response;

};

struct EqualizerView {
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
};


class PitchShifter: IFreqEffect {
private:
    static inline const float MIN_STRETCH_K = 0.05;
    float stretch_k = 1.0f;
public:
    void setStretch(float stretch) {
        stretch_k = std::max(MIN_STRETCH_K, stretch);
    }

    float getStretch() const { return stretch_k; }
    void setPitch(int octaves, int semitones, int cents);

    void operator()(std::vector<kiss_fft_cpx> &freq_data) override;
    ~PitchShifter() override = default;
};

// class ChainEffectProcessor {
// private:
//     using BlockProcessor = std::function<void(std::vector<kiss_fft_cpx>&)>;
//     std::vector<std::shared_ptr<IFreqEffect>> effects_;

//     using MetaInfo = int64_t;
//     std::vector<MetaInfo> effects_meta_;

// public:
//     template<typename ProcessorT>
//     void addEffect(ProcessorT &processor, MetaInfo meta = {}) {
//         effects_.push_back(std::bind(&processor.operator(), &processor));
//         effects_meta_.push_back(meta);
//     }

//     void popEffect() {
//         effects_.pop_back();
//         effects_meta_.pop_back();
//     }

//     void operator()(std::vector<kiss_fft_cpx> &freq_data) {
//         for (int i = 0; i < effects_.size() ; i++) {
//             effects_[i](freq_data);
//         }
//     }

// };

struct Fade {
    enum FADE_DIRECTION { IN = 0, OUT = 1 };
    FADE_DIRECTION direction;
    int64_t duration; // duration in frames

    float getGain(int64_t pivot, int64_t frame) const {
        if (duration == 0) return 1.0f;

        switch (direction) {
            case IN: {
                return std::clamp(static_cast<float>(frame - pivot) / duration, 0.f, 1.f);
            } break;
            case OUT: {
                return std::clamp(static_cast<float>(pivot - frame) / duration, 0.f, 1.f);
            } break;
            default: return 1.0f;
        }
    }
};

}
