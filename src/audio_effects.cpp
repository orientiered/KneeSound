#include <algorithm>
#include <vector>
#include "audio_effects.h"
#include "common.h"

/* ================= Equalizer ================== */

namespace waves {

void FreqDomainEffect::prepareTimeData(AudioBuffer &in, size_t ch_idx) {
    // time_data: | previous block | data |
    // 2*hop_size     hop_size      hop_size
    audio_sample_t *prev_block = previous_block_.getChannel(ch_idx);
    audio_sample_t *cur_block = in.getChannel(ch_idx);

    std::copy_n(prev_block, hop_size_, time_data.begin());

    // saving current block for next call
    std::copy_n(cur_block, hop_size_, prev_block);

    // filling time_data for fft
    std::copy_n(cur_block, hop_size_, time_data.begin() + hop_size_);

}

void FreqDomainEffect::applyOverlap(AudioBuffer &out, size_t ch_idx) {

    audio_sample_t *overlap = overlap_add_.getChannel(ch_idx);
    audio_sample_t *out_ch = out.getChannel(ch_idx);

    for (size_t idx = 0; idx < hop_size_; idx++) {
        // output[i] = overlapp_add[i] + time_data[i], i = 0...hop-1
        out_ch[idx] = time_data[idx] + overlap[idx];
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

BiquadSettings::Coeffs BiquadSettings::makeLPF(double fc, double Q, double gainDb, double fs) {
    RBJ_Params p(fc, Q, gainDb, fs);

    double b1 = 1.0 - p.cos_w0;
    double b0 = b1 / 2.0;
    double b2 = b1 / 2.0;

    double a0 = 1.0 + p.alpha;
    double a1 = -2.0 * p.cos_w0;
    double a2 = 1.0 - p.alpha;

    return Coeffs(b0, b1, b2, a0, a1, a2);
}

BiquadSettings::Coeffs BiquadSettings::makeHPF(double fc, double Q, double gainDb, double fs) {
    RBJ_Params p(fc, Q, gainDb, fs);

    double b1 = -(1.0 - p.cos_w0);
    double b0 = -b1 / 2.0;
    double b2 = -b1 / 2.0;

    double a0 = 1.0 + p.alpha;
    double a1 = -2.0 * p.cos_w0;
    double a2 = 1.0 - p.alpha;

    return Coeffs(b0, b1, b2, a0, a1, a2);
}

BiquadSettings::Coeffs BiquadSettings::makeBPF(double fc, double Q, double gainDb, double fs) {
    RBJ_Params p(fc, Q, gainDb, fs);

    double b0 = Q * p.alpha;
    double b1 = 0;
    double b2 = -Q * p.alpha;

    double a0 = 1.0 + p.alpha;
    double a1 = -2.0 * p.cos_w0;
    double a2 = 1.0 - p.alpha;

    return Coeffs(b0, b1, b2, a0, a1, a2);
}

BiquadSettings::Coeffs BiquadSettings::makeNotch(double fc, double Q, double gainDb, double fs) {

    RBJ_Params p(fc, Q, gainDb, fs);

    double b0 = 1.0;
    double b1 = -2.0 * p.cos_w0;
    double b2 = 1.0;
    double a0 = 1.0 + p.alpha;
    double a1 = -2.0 * p.cos_w0;
    double a2 = 1.0 - p.alpha;

    return Coeffs(b0, b1, b2, a0, a1, a2);
}

BiquadSettings::Coeffs BiquadSettings::makePeaking(double fc, double Q, double gainDb, double fs) {

    RBJ_Params p(fc, Q, gainDb, fs);

    double a0 = 1.0 + p.alpha / p.A;
    double b0 = (1.0 + p.alpha * p.A);
    double b1 = (-2.0 * p.cos_w0);
    double b2 = (1.0 - p.alpha * p.A);
    double a1 = (-2.0 * p.cos_w0);
    double a2 = (1.0 - p.alpha / p.A);

    return Coeffs(b0, b1, b2, a0, a1, a2);
}


}
