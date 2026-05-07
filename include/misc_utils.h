#pragma once

#include <vector>
#include "kiss_fft.h"

/**
 * Конвертирует линейно распределённые частотные бины в двойной логарифмический масштаб.
 *
 * @param linearBins Вектор комплексных значений [0..N/2], где bins[k] = H(k * fs / FFT_size)
 * @param sampleRate Частота дискретизации в Гц
 * @param fftSize Размер БПФ (количество точек, по которым считался спектр)
 * @param outputPoints Количество точек на выходе (разрешение графика)
 * @param minFreq Минимальная частота для отображения (по умолчанию 20 Гц)
 * @param maxFreq Максимальная частота (0 = использовать Найквист)
 * @param useDb Если true — вернуть дБ, иначе — линейный логарифм
 * @return Вектор значений для отрисовки: y[i] = response(f_out[i])
 */
std::vector<float> convertToDoubleLogScale(
    const std::vector<kiss_fft_cpx>& linearBins,
    float sampleRate,
    size_t fftSize,
    size_t outputPoints = 512,
    float minFreq = 20.0f,
    float maxFreq = 0.0f,
    bool useDb = true
);

std::vector<float> convertToDoubleLogScale(
    const std::vector<float>& linearBins,
    float sampleRate,
    size_t fftSize,
    size_t outputPoints = 512,
    float minFreq = 20.0f,
    float maxFreq = 0.0f,
    bool useDb = true
);
