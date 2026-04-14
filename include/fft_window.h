#pragma once

#include "common.h"
#include "math.h"

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
        return 0.5f * (1.0f - std::cos(2.0f * M_PI * n / (size - 1)));
    }
    static float Hamming(size_t n, size_t size) {
        return 0.54f - 0.46f * std::cos(2.0f * M_PI * n / (size - 1));
    }
    static float Blackman(size_t n, size_t size) {
        return 0.42f 
                - 0.5f  * std::cos(2.0f * M_PI * n / (size - 1))
                + 0.08f * std::cos(4.0f * M_PI * n / (size - 1));
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
}