#pragma once

#include "common.h"
#include "math.h"
#include "kiss_fft.h"
#include "kiss_fftr.h"

namespace waves {

// Window functions for FFT
class WindowFunction {
public:
    enum class Type {
        None = 0,
        Rectangle = None,
        Hann,
        Hamming,
        Blackman,
    };

    static float Rect(size_t n, size_t size) {
        return 1.0f;
    }
    static float Hann(size_t n, size_t size) {
        return 0.5f * (1.0f - std::cos(2.0f * M_PI * static_cast<float>(n) / static_cast<float>(size) ));
    }
    static float Hamming(size_t n, size_t size) {
        return 0.54f - 0.46f * std::cos(2.0f * M_PI * n / size );
    }
    static float Blackman(size_t n, size_t size) {
        return 0.42f
                - 0.5f  * std::cos(2.0f * M_PI * n / size )
                + 0.08f * std::cos(4.0f * M_PI * n / size );
    }

    using window_func_t = float (*)(size_t n, size_t size);

    static std::vector<float> generate(Type type, size_t size) {
        std::vector<float> window(size);

        window_func_t window_func = &WindowFunction::Rect;

        switch (type) {
            case Type::Hann:      window_func = &Hann; break;
            case Type::Hamming:   window_func = &Hamming; break;
            case Type::Blackman:  window_func = &Blackman; break;
            case Type::Rectangle: window_func = &Rect; break;
            default:              window_func = &Rect; break;
        }

        for (size_t n = 0; n < size; n++) {
            window[n] = window_func(n, size);
        }

        return window;
    }

    // === Применение окна к буферу ===
    template<size_t Tchannels>
    static void applyInPlace(const std::vector<float>& window, float* data, size_t frame_count) {
        assert(window.size() == frame_count);
        for (size_t i = 0; i < frame_count; i++) {
            for(size_t ch_idx = 0; ch_idx < Tchannels; ch_idx++) {
                data[Tchannels * i + ch_idx] *= window[i];
            }
        }
    }

    template<size_t Tchannels>
    static void apply(const std::vector<float>& window,
                      const float* input,
                      float* output,
                      size_t frame_count) {
        for (size_t i = 0; i < frame_count; i++) {
            for (size_t ch_idx = 0; ch_idx < Tchannels; ch_idx++) {
                output[Tchannels * i + ch_idx] = input[Tchannels * i + ch_idx] * window[i];
            }
        }
    }
};

class KissFFTR {
private:
    kiss_fftr_cfg forward_cfg = nullptr;
    kiss_fftr_cfg inverse_cfg = nullptr;
    size_t nfft_ = 0;

public:
    KissFFTR() {}
    KissFFTR(size_t nfft, bool forward = true, bool inverse = false) {
        if (nfft % 2 != 0)
            throw std::invalid_argument("Nfft must be even");

        nfft_ = nfft;

        if (forward)
            forward_cfg = kiss_fftr_alloc(nfft, 0, NULL, NULL);
        if (inverse)
            inverse_cfg = kiss_fftr_alloc(nfft, 1, NULL, NULL);

    }

    bool hasForward() const noexcept { return forward_cfg; }
    bool hasInverse() const noexcept { return inverse_cfg; }
    size_t getNfft() const noexcept { return nfft_; }

    bool forward(float *time_data, kiss_fft_cpx *freq_data) {
        if (!forward_cfg) return false;

        kiss_fftr(forward_cfg, time_data, freq_data);
        return true;
    }

    bool inverse(kiss_fft_cpx *freq_data, float *time_data) {
        if (!inverse_cfg) return false;

        kiss_fftri(inverse_cfg, freq_data, time_data);

        return true;
    }

    void normalize(float *time_data) {
        // normalization
        const float scale = 1.0f / static_cast<float>(getNfft());
        for(size_t i = 0; i < getNfft(); ++i) {
            time_data[i] *= scale;
        }
    }

    KissFFTR(const KissFFTR& other) :
        KissFFTR(other.nfft_, other.forward_cfg, other.inverse_cfg) {}

    KissFFTR& operator=(KissFFTR other) {
        swap(*this, other);
        return *this;
    }

    KissFFTR(KissFFTR &&other) noexcept : KissFFTR() {
        swap(*this, other);
    }

    friend void swap(KissFFTR &first, KissFFTR &second) noexcept {
        std::swap(first.forward_cfg, second.forward_cfg);
        std::swap(first.inverse_cfg, second.inverse_cfg);
        std::swap(first.nfft_, second.nfft_);
    }


    ~KissFFTR() {
        KISS_FFT_FREE(forward_cfg);
        KISS_FFT_FREE(inverse_cfg);
        nfft_ = 0;
    }
};

class WindowedKissFFTR {
    KissFFTR fftr;
    std::vector<float> window_;
    WindowFunction::Type window_type_ = WindowFunction::Type::Hann;
public:
    WindowedKissFFTR() {}
    WindowedKissFFTR(size_t nfft, WindowFunction::Type window_type, bool forward = true, bool inverse = false):
        window_type_(window_type),
        fftr(nfft, forward, inverse) {
        window_ = WindowFunction::generate(window_type, nfft);
    }

    bool hasForward() const noexcept { return fftr.hasForward(); }
    bool hasInverse() const noexcept { return fftr.hasInverse(); }
    size_t getNfft() const noexcept { return fftr.getNfft(); }
    WindowFunction::Type getType() const noexcept { return window_type_; }

    void recalcWindow(WindowFunction::Type window_type) {
        if (window_type == window_type_) return;

        window_ = WindowFunction::generate(window_type, getNfft());
        window_type_ = window_type;
    }

    bool forward(float *time_data, kiss_fft_cpx *freq_data) {
        if (!hasForward()) return false;

        WindowFunction::applyInPlace<1>(window_, time_data, fftr.getNfft());
        return fftr.forward(time_data, freq_data);
    }

    bool inverse(kiss_fft_cpx *freq_data, float *time_data) {
        if (!hasInverse()) return false;

        return fftr.inverse(freq_data, time_data);
    }

    void normalize(float *time_data) { fftr.normalize(time_data); }


};

}
