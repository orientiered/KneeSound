#include "effects/biquad_filter.h"
#include "common.h"
#include "imgui_misc.h"
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

    modified |= ImGui::SliderFloat("Central freq", &central_freq, 0, static_cast<float>(INNER_SAMPLE_RATE) / 2, "%.0f Hz");
    modified |= ImGui::SliderFloat("Q", &Q, 0, 10000, "%.1f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::SliderFloat("Gain", &gainDb, -30, +40, "%.1f db");

    if (modified) {
        Coeffs cfs;

        switch(preset) {
            case LOWPASS:
                cfs = makeLPF(central_freq, Q, gainDb, sample_freq);
                break;
            case HIGHPASS:
                cfs = makeHPF(central_freq, Q, gainDb, sample_freq);
                break;
            case BANDPASS:
                cfs = makeBPF(central_freq, Q, gainDb, sample_freq);
                break;
            case NOTCH:
                cfs = makeNotch(central_freq, Q, gainDb, sample_freq);
                break;
            case PEAKING:
                cfs = makePeaking(central_freq, Q, gainDb, sample_freq);
                break;
            default:
                break;
        }

        bqf->setCoefficients(cfs.f1, cfs.f2, cfs.f3, cfs.g1, cfs.g2);
    }
}


}
