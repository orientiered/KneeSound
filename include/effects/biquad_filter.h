#pragma once

#include "common.h"
#include "effects/audio_effects.h"

namespace waves {

class BiquadFilter : public IDspKernel {
private:
    // Filter coeffs
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // Filter state
    struct FilterState {
        float s1 = 0.0f, s2 = 0.0f;
    };

    std::vector<FilterState> states;
public:
    BiquadFilter(uint32_t channels = INNER_CHANNELS): states(channels) {}

    void setCoefficients(float f1, float f2, float f3, float g1, float g2) {
        b0 = f1; b1 = f2; b2 = f3;
        a1 = g1; a2 = g2;
    }

    // One sample processing, Transposed direct form 2
    inline float processSample(FilterState &s, float x) {
        float y = b0 * x + s.s1;
        s.s1 = b1 * x + s.s2 - a1 * y;
        s.s2 = b2 * x - a2 * y;
        return y;
    }

    void process(const AudioBuffer& in, AudioBuffer &out) override {
        const size_t numSamples = in.getFrameCount();

        for (size_t ch = 0; ch < in.getChannels(); ch++) {
            const audio_sample_t *ch_in = in[ch];
            audio_sample_t *ch_out = out.getChannel(ch);

            for (size_t i = 0; i < numSamples; ++i) {
                ch_out[i] = processSample(states[ch], ch_in[i]);
            }
        }
    }

    uint32_t getLatencySamples() const override {
        return 0;
    }

    void reset() override {
        std::fill(states.begin(), states.end(), FilterState());
    }

    ~BiquadFilter() override = default;
};


class BiquadSettings : public IEffectView {
    struct Coeffs {
        float f1, f2, f3; // numerator
        float g1, g2;     // denumerator (with a0=1, normalized form)

        Coeffs() {}

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

    enum Preset {
        NONE = -1,
        LOWPASS = 0,
        HIGHPASS,
        BANDPASS,
        NOTCH,
        PEAKING
    };

    Preset preset = LOWPASS;

    float central_freq = 1000, Q = 1, gainDb = 10;
    float sample_freq = INNER_SAMPLE_RATE;

    const size_t FREQ_RESPONSE_POINTS = 150;
    bool digitalResponse = true; // false -> use analog filter
    std::vector<float> log_freq_response;
public:
    // coeffs calculating
    Coeffs makeLPF(double fc, double Q, double gainDb, double fs);
    Coeffs makeHPF(double fc, double Q, double gainDb, double fs);
    Coeffs makeBPF(double fc, double Q, double gainDb, double fs);
    Coeffs makeNotch(double fc, double Q, double gainDb, double fs);
    Coeffs makePeaking(double fc, double Q, double gainDb, double fs);
    // calculate based on current preset
    Coeffs calculateCoeffs();

    void DrawSettings() override;
    BiquadSettings(BiquadFilter *filter): bqf(filter), log_freq_response(FREQ_RESPONSE_POINTS) {
        updateKernelCoeffs();
    }

    // recalc log_freq_response
    void updateFreqResponse();

    void updateKernelCoeffs() {
        Coeffs cfs = calculateCoeffs();
        bqf->setCoefficients(cfs.f1, cfs.f2, cfs.f3, cfs.g1, cfs.g2);
        updateFreqResponse();
    }

    ~BiquadSettings() override = default;
private:
    BiquadFilter *bqf = nullptr;
};

class BiquadFactory: public IEffectFactory {
public:
    ~BiquadFactory() override = default;
    EffectDescriptor getDescriptor() override {
        return {
            .name = "Biquad filter",
            .version = "1.0",
            .id = "knee_biquad_filter"
        };
    }

    PluginPair build() override {
        std::unique_ptr<BiquadFilter> kernel = std::make_unique<BiquadFilter>();
        std::unique_ptr<IEffectView> view = std::make_unique<BiquadSettings>(kernel.get());

        return {std::move(kernel), std::move(view)};
    }
};


}
