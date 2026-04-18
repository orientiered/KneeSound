#pragma once

#include "common.h"

#include "kiss_fftr.h"
#include "fft_utils.h"
#include "algorithm"

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

    size_t getLatency() const noexcept { return 1; } ///< Latency in blocks
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

/* ========================== EQUALIZER ======================== */
class Equalizer {
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

    void operator()(std::vector<kiss_fft_cpx> &freq_data) {
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
};


struct EqualizerSettings {
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