#pragma once

#include "common.h"
#include "buffer_utils.h"

#include "kiss_fft.h"
#include "fft_utils.h"
#include "algorithm"
#include <cmath>
#include <memory>

namespace waves {

// Db to linear conversion
inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

/* =================== Base interface for effects =============== */
class IDspKernel {
public:
    virtual ~IDspKernel() = default;
    // virtual void prepare(double sampleRate, uint32_t blockSize) = 0;
    virtual void process(const AudioBuffer& in, AudioBuffer &out) = 0;
    virtual uint32_t getLatencySamples() const = 0;
    virtual void reset() = 0;
};

using StateWriter = std::ostream;
using StateReader = std::istream;

/* ============== Base interface for effect editor/view-controller ========== */
class IEffectView {
public:
    virtual ~IEffectView() = default;
    virtual void DrawSettings() = 0;
    // virtual void serialize(StateWriter &out);
    // virtual void deserialize(StateReader &in);

};

/* =============== Effect slot with kernel and view ======================================== */
class EffectSlot {
public:
    EffectSlot(std::unique_ptr<IDspKernel> k, std::unique_ptr<IEffectView> v):
        kernel(std::move(k)), view(std::move(v)) {}

    std::unique_ptr<IDspKernel> kernel;
    std::unique_ptr<IEffectView> view;
    std::string name;
};


/* ================ GENERAL PIPELINE FOR EFFECT IN FREQUENCY DOMAIN ========== */
/// 0. For each channel:
/// 1. Concat data[block_size] with previous segment
/// 2. Perform real fft with Hann window -> freq_data[block_size+1]
/// 3. Apply given effect with 'Processor' callable on freq_data
/// 4. Inverse fft with normalization
/// 5. Overlapp add with previous calculated segment
/// Important: due to overlapp-add has latency of 1 block size
class FreqDomainPipeline {
private:
    WindowedKissFFTR wfftr_;

    size_t hop_size_;
    size_t channels_;

    // overlap and previous are stored sequentially, not interleaved
    AudioBuffer overlap_add_;
    AudioBuffer previous_block_;

    std::vector<audio_sample_t> time_data; ///< array for temporary calculations
    std::vector<kiss_fft_cpx>   freq_data; ///< array for temporary calculations

public:
    FreqDomainPipeline(size_t block_size, size_t channels):
        wfftr_(2*block_size, WindowFunction::Type::Hann, true, true),
        hop_size_(block_size), channels_(channels),
        overlap_add_(hop_size_, channels_),
        previous_block_(hop_size_, channels_),
        time_data(2*block_size, 0.0f),
        freq_data(block_size + 1) {}

    size_t getLatency() const noexcept { return hop_size_; } ///< Latency in samples
    size_t getBlockSize() const noexcept { return hop_size_; }
    size_t getFreqDataSize() const noexcept { return hop_size_ + 1; }

    void reset() {
        overlap_add_.clear();
        previous_block_.clear();
    }

    // in and out may be the same
    template<typename Processor>
    void processBlock(const AudioBuffer &in, AudioBuffer &out, Processor &processor) {
        for (size_t ch_idx = 0; ch_idx < channels_; ch_idx++) {

            prepareTimeData(in, ch_idx);

            wfftr_.forward(time_data.data(), freq_data.data());

            processor.processFreqData(freq_data);

            wfftr_.inverse(freq_data.data(), time_data.data());
            wfftr_.normalize(time_data.data());

            applyOverlap(out, ch_idx);
        }

    }

private:
    void prepareTimeData(const AudioBuffer &in, size_t ch_idx);
    void applyOverlap(AudioBuffer &out, size_t ch_idx);
};


// class PitchShifter: IDspKernel {
// private:
//     static inline const float MIN_STRETCH_K = 0.05;
//     float stretch_k = 1.0f;
// public:
//     void setStretch(float stretch) {
//         stretch_k = std::max(MIN_STRETCH_K, stretch);
//     }

//     float getStretch() const { return stretch_k; }
//     void setPitch(int octaves, int semitones, int cents);

//     void operator()(std::vector<kiss_fft_cpx> &freq_data) override;
//     ~PitchShifter() override = default;
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
