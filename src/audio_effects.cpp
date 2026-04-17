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

}