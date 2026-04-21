#include <algorithm>
#include "audio_effects.h"

/* ================= Equalizer ================== */

namespace waves {

void FreqDomainEffect::prepareTimeData(audio_sample_t *in, size_t ch_idx) {
    // time_data: | previous block | data |
    // 2*hop_size     hop_size      hop_size
    audio_sample_t *prev_block = &previous_block_[ch_idx*hop_size_];
    std::copy_n(prev_block, hop_size_, time_data.begin());

    for (size_t idx = 0; idx < hop_size_; idx++) {
        audio_sample_t sample = in[idx * channels_ + ch_idx];

        // saving current block for next call
        prev_block[idx] = sample;

        // filling time_data for fft
        time_data[hop_size_ + idx] = sample;
    }

}

void FreqDomainEffect::applyOverlap(audio_sample_t *out, size_t ch_idx) {
    
    audio_sample_t *overlap = &overlap_add_[ch_idx*hop_size_];

    for (size_t idx = 0; idx < hop_size_; idx++) {
        // output[i] = overlapp_add[i] + time_data[i], i = 0...hop-1
        out[idx * channels_ + ch_idx] = time_data[idx] + overlap[idx];
    }

    // overlapp_add[i] = time_data[i], i = hop...2*hop-1
    std::copy_n(time_data.begin() + hop_size_, hop_size_, overlap);
}

void PitchShifter::operator()(std::vector<kiss_fft_cpx> &freq_data) {
    const size_t size = freq_data.size();

    auto getInterpolatedFreq = [&](float idx) -> kiss_fft_cpx {
        if (idx < 0 || idx >= size) return {0, 0};
        int left_idx = std::floor(idx);
        float p = idx - left_idx;

        kiss_fft_cpx left = freq_data[left_idx];
        kiss_fft_cpx right = (left_idx < (size - 1)) ? freq_data[left_idx+1] :
                                                       kiss_fft_cpx{0, 0};
        kiss_fft_cpx result = {
            left.r * (1-p) + right.r * p,
            left.i * (1-p) + right.i * p
        };
        return result;
    };

    if (stretch_k > 1) {
        for (int i = 0; i < size; i++) {
            freq_data[i] = getInterpolatedFreq(i    *stretch_k);
        }
    } else {
        for (int i = size-1; i >= 0; i--) {
            freq_data[i] = getInterpolatedFreq(i*stretch_k);
        }
    }
}


}