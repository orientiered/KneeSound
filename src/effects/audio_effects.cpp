#include <algorithm>
#include <vector>
#include "audio_effects.h"
#include "common.h"

/* ================= Equalizer ================== */

namespace waves {

void FreqDomainPipeline::prepareTimeData(const AudioBuffer &in, size_t ch_idx) {
    // time_data: | previous block | data |
    // 2*hop_size     hop_size      hop_size
    audio_sample_t *prev_block = previous_block_.getChannel(ch_idx);
    const audio_sample_t *cur_block = in[ch_idx];

    std::copy_n(prev_block, hop_size_, time_data.begin());

    // saving current block for next call
    std::copy_n(cur_block, hop_size_, prev_block);

    // filling time_data for fft
    std::copy_n(cur_block, hop_size_, time_data.begin() + hop_size_);

}

void FreqDomainPipeline::applyOverlap(AudioBuffer &out, size_t ch_idx) {

    audio_sample_t *overlap = overlap_add_.getChannel(ch_idx);
    audio_sample_t *out_ch = out.getChannel(ch_idx);

    for (size_t idx = 0; idx < hop_size_; idx++) {
        // output[i] = overlapp_add[i] + time_data[i], i = 0...hop-1
        out_ch[idx] = time_data[idx] + overlap[idx];
    }

    // overlapp_add[i] = time_data[i], i = hop...2*hop-1
    std::copy_n(time_data.begin() + hop_size_, hop_size_, overlap);
}

// void PitchShifter::operator()(std::vector<kiss_fft_cpx> &freq_data) {
//     const size_t size = freq_data.size();

//     auto getInterpolatedFreq = [&](float idx) -> kiss_fft_cpx {
//         if (idx < 0 || idx >= size) return {0, 0};
//         int left_idx = std::floor(idx);
//         float p = idx - left_idx;

//         kiss_fft_cpx left = freq_data[left_idx];
//         kiss_fft_cpx right = (left_idx < (size - 1)) ? freq_data[left_idx+1] :
//                                                        kiss_fft_cpx{0, 0};
//         kiss_fft_cpx result = {
//             left.r * (1-p) + right.r * p,
//             left.i * (1-p) + right.i * p
//         };
//         return result;
//     };

//     if (stretch_k > 1) {
//         for (int i = 0; i < size; i++) {
//             freq_data[i] = getInterpolatedFreq(i    *stretch_k);
//         }
//     } else {
//         for (int i = size-1; i >= 0; i--) {
//             freq_data[i] = getInterpolatedFreq(i*stretch_k);
//         }
//     }
// }

}
