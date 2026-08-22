#include "effects/biquad_filter.h"
#include "common.h"
#include "imgui_misc.h"
#include "serialization.h"
#include <endian.h>
#include <imgui.h>
namespace waves {

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

    double b1 = -(1.0 + p.cos_w0);
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

BiquadSettings::Coeffs BiquadSettings::calculateCoeffs() {
    switch(preset) {
        case LOWPASS:
            return makeLPF(central_freq, Q, gainDb, sample_freq);
        case HIGHPASS:
            return makeHPF(central_freq, Q, gainDb, sample_freq);
        case BANDPASS:
            return makeBPF(central_freq, Q, gainDb, sample_freq);
        case NOTCH:
            return makeNotch(central_freq, Q, gainDb, sample_freq);
        case PEAKING:
            return makePeaking(central_freq, Q, gainDb, sample_freq);
        default:
            return {1.0, 0, 0, 0, 0};
    }
}

void BiquadSettings::updateFreqResponse() {
    const size_t points = log_freq_response.size();

    RBJ_Params p(central_freq, Q, gainDb, sample_freq);

    auto cmod = [] (double re, double im) {
        return sqrt(re*re + im * im);
    };

    auto cvt2Db = [](float gain) -> float {
        return 20 * std::log10(std::max(gain, 1e-10f));
    };

    auto calcAnalogResponse = [&] (Preset preset, double freq) -> float {
        double s = freq / central_freq;  // / sample_freq; // s = j * w / w0
        switch(preset) {
            case LOWPASS:
                // H(s) = 1 / (s^2 + s/Q + 1)
                return 1. / cmod(1-s*s, s / Q);
            case HIGHPASS:
                // H(s) = s^2 / (s^2 + s/Q + 1)
                return s*s / cmod(1-s*s, s / Q);
            case BANDPASS:
                // H(s) = s / (s^2 + s/Q + 1)  (constant skirt gain, peak gain = Q)
                return s / cmod(1-s*s, s / Q);
            case NOTCH:
                // H(s) = (s^2 + 1) / (s^2 + s/Q + 1)
                return (1 - s*s) / cmod(1-s*s, s / Q);
            case PEAKING:
                // H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1)
                return cmod(1-s*s, s * (p.A / Q)) / cmod(1-s*s, s / (p.A * Q));;
            default:
                return 1.0;
        }
    };

    auto calcDigitalResponse = [&](Coeffs cfs, double freq) -> float {
        double omega = 2.0 * M_PI * freq / sample_freq;  // нормированная частота [0, π]

        // z⁻¹ = e^(-jω) = cos(ω) - j·sin(ω)
        double cos_omega = std::cos(omega);
        double sin_omega = std::sin(omega);

        // z⁻² = e^(-j2ω) = cos(2ω) - j·sin(2ω)
        double cos_2omega = std::cos(2.0 * omega);
        double sin_2omega = std::sin(2.0 * omega);

        // Числитель: b0 + b1·z⁻¹ + b2·z⁻²
        double num_re = cfs.f1 + cfs.f2 * cos_omega + cfs.f3 * cos_2omega;
        double num_im =        - cfs.f2 * sin_omega - cfs.f3 * sin_2omega;  // j-часть с минусом

        // Знаменатель: 1 + a1·z⁻¹ + a2·z⁻²
        double den_re = 1.0 + cfs.g1 * cos_omega + cfs.g2 * cos_2omega;
        double den_im =        - cfs.g1 * sin_omega - cfs.g2 * sin_2omega;

        // |H(z)| = |num| / |den|
        double num_mag = cmod(num_re, num_im);
        double den_mag = cmod(den_re, den_im);

        return static_cast<float>(num_mag / std::max(den_mag, 1e-10));
    };


    double min_freq = 20.0f;
    double max_freq = sample_freq / 2.0f;

    Coeffs cfs = calculateCoeffs();

    for (int i = 0; i < points; i++) {
        double freq  = min_freq * std::pow(max_freq / min_freq, static_cast<double>(i) / (points-1));

        freq = std::clamp(freq, min_freq, max_freq * 0.999);

        if (digitalResponse) {
            log_freq_response[i] = cvt2Db(calcDigitalResponse(cfs, freq));
        } else {
            log_freq_response[i] = cvt2Db(calcAnalogResponse(preset, freq));
        }
    }

    // PLOG_DEBUG << log_freq_response;
}

void BiquadSettings::DrawSettings() {
    bool modified = false;

    const char *filter_type_str[] = {
        "Lowpass",
        "Highpass",
        "Bandpass",
        "Notch",
        "Peaking"
    };

    if (ImGui::ListBox("Filter type", reinterpret_cast<int*>(&preset), filter_type_str, 5)) {
        modified = true;
    }

    modified |= ImGui::DragFloat("Central freq", &central_freq, 2.0, 0, static_cast<float>(INNER_SAMPLE_RATE) / 2, "%.0f Hz");
    modified |= ImGui::DragFloat("Q", &Q, 2.0, 0, 10000, "%.1f", ImGuiSliderFlags_Logarithmic);

    if (preset == PEAKING) {
        // other types do not use gain
        modified |= ImGui::DragFloat("Gain", &gainDb, 0.4, -30, +40, "%.1f db");
    }

    if (modified) {
        updateKernelCoeffs();
    }

    ImGui::PlotLines("Frequency response", log_freq_response.data(), log_freq_response.size(),
        0, nullptr, -100, 10, ImVec2(0, ImGui::GetFrameHeight() * 4));

    ImGui::Text("Uncheck to see analog prototype response");
    if (ImGui::Checkbox("Analog/Digital", &digitalResponse)) {
        updateFreqResponse();
    }


}

void BiquadSettings::serialize(ProjectWriter output) const {
    // enum preset -> int
    SERIALIZE_SIMPLE(output, preset);
    SERIALIZE_SIMPLE(output, central_freq);
    SERIALIZE_SIMPLE(output, Q);
    SERIALIZE_SIMPLE(output, gainDb);
    SERIALIZE_SIMPLE(output, sample_freq);
    SERIALIZE_SIMPLE(output, digitalResponse);
}

void BiquadSettings::deserialize(ProjectReader input) {
    preset = static_cast<Preset>(input.read<int>("preset", LOWPASS));
    DESERIALIZE_OPT(input, central_freq);
    DESERIALIZE_OPT(input, Q);
    DESERIALIZE_OPT(input, gainDb);
    DESERIALIZE_OPT(input, sample_freq);
    DESERIALIZE_OPT(input, digitalResponse);

    updateKernelCoeffs();
}

}
