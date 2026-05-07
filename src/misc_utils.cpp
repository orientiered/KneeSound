#include "misc_utils.h"
#include "kiss_fft.h"

#include <cmath>
#include <algorithm>


std::vector<float> convertToDoubleLogScale(
    const std::vector<kiss_fft_cpx>& linearBins,
    float sampleRate,
    size_t fftSize,
    size_t outputPoints,
    float minFreq,
    float maxFreq,
    bool useDb
) {
    if (linearBins.empty() || fftSize < 2) return {};

    const float nyquist = sampleRate * 0.5f;
    if (maxFreq <= 0.0f || maxFreq > nyquist) maxFreq = nyquist;
    if (minFreq < 1.0f) minFreq = 1.0f;  // защита от log(0)
    if (minFreq >= maxFreq) minFreq = maxFreq * 0.1f;

    std::vector<float> result(outputPoints);

    const float binWidth = sampleRate / static_cast<float>(fftSize);

    auto interpolateComplex = [&](float binIndex) -> kiss_fft_cpx {
        const size_t k0 = static_cast<size_t>(std::floor(binIndex));
        const size_t k1 = std::min(k0 + 1, linearBins.size() - 1);
        const float t = binIndex - static_cast<float>(k0);

        if (k0 >= linearBins.size()) return linearBins.back();
        return {linearBins[k0].r * (1.0f - t) + linearBins[k1].r * t,
                linearBins[k0].i * (1.0f - t) + linearBins[k1].i * t};
    };

    for (size_t i = 0; i < outputPoints; ++i) {
        // 1. Логарифмически распределённая частота
        const double t = static_cast<double>(i) / static_cast<double>(outputPoints - 1);
        const double freq = minFreq * std::pow(maxFreq / minFreq, t);

        // 2. Соответствующий индекс бина на линейной сетке
        const float binIndex = static_cast<float>(freq / binWidth);
        const float clampedBin = std::clamp(binIndex, 0.0f, static_cast<float>(linearBins.size() - 1));

        // 3. Интерполяция и вычисление отклика
        const kiss_fft_cpx H = interpolateComplex(clampedBin);
        const float magnitude = std::sqrt(H.r * H.r + H.i * H.i);

        // 4. Преобразование в логарифмическую шкалу
        if (useDb) {
            result[i] = 20.0f * std::log10(std::max(magnitude, 1e-10f));
        } else {
            result[i] = std::log10(std::max(magnitude, 1e-10f));
        }
    }

    return result;
}

std::vector<float> convertToDoubleLogScale(
    const std::vector<float>& linearBins,
    float sampleRate,
    size_t fftSize,
    size_t outputPoints,
    float minFreq,
    float maxFreq,
    bool useDb
) {
    if (linearBins.empty() || fftSize < 2) return {};

    const float nyquist = sampleRate * 0.5f;
    if (maxFreq <= 0.0f || maxFreq > nyquist) maxFreq = nyquist;
    if (minFreq < 1.0f) minFreq = 1.0f;  // защита от log(0)
    if (minFreq >= maxFreq) minFreq = maxFreq * 0.1f;

    std::vector<float> result(outputPoints);

    const float binWidth = sampleRate / static_cast<float>(fftSize);

    auto interpolate = [&](float binIndex) -> float {
        const size_t k0 = static_cast<size_t>(std::floor(binIndex));
        const size_t k1 = std::min(k0 + 1, linearBins.size() - 1);
        const float t = binIndex - static_cast<float>(k0);

        if (k0 >= linearBins.size()) return linearBins.back();
        return linearBins[k0] * (1.0f - t) + linearBins[k1] * t;
    };

    for (size_t i = 0; i < outputPoints; ++i) {
        // 1. Логарифмически распределённая частота
        const double t = static_cast<double>(i) / static_cast<double>(outputPoints - 1);
        const double freq = minFreq * std::pow(maxFreq / minFreq, t);

        // 2. Соответствующий индекс бина на линейной сетке
        const float binIndex = static_cast<float>(freq / binWidth);
        const float clampedBin = std::clamp(binIndex, 0.0f, static_cast<float>(linearBins.size() - 1));

        // 3. Интерполяция и вычисление отклика
        const float magnitude = interpolate(clampedBin);

        // 4. Преобразование в логарифмическую шкалу
        if (useDb) {
            result[i] = 20.0f * std::log10(std::max(magnitude, 1e-10f));
        } else {
            result[i] = std::log10(std::max(magnitude, 1e-10f));
        }
    }

    return result;
}
