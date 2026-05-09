#include "common.h"
#include "effects/fft_equalizer.h"

#include "imgui.h"
#include "imgui_misc.h"

#include "utils/misc_utils.h"

namespace waves {

bool FFT_EqualizerView::DrawLowpass() {
    ImGui::IdGuard ig(&lowpass);
    bool modified = setPreset(LOWPASS);
    modified |= ImGui::DragFloat("Cutoff", &lowpass.cutoff, 3, MIN_FREQ, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation", &lowpass.attenuation, 1, 0, 100, "%.2f");
    if (modified)
        calculateLowpass();

    return modified;
}

bool FFT_EqualizerView::DrawHighpass() {
    ImGui::IdGuard ig(&highpass);
    bool modified = setPreset(HIGHPASS);
    modified |= ImGui::DragFloat("Cutoff", &highpass.cutoff, 3, MIN_FREQ, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation", &highpass.attenuation, 1, 0, 100, "%.2f");
    if (modified)
        calculateHighpass();

    return modified;
}

bool FFT_EqualizerView::DrawBandpass() {
    bool modified = setPreset(BANDPASS);
    ImGui::IdGuard ig(&bandpass);
    modified |= ImGui::DragFloat("Cutoff left", &bandpass.left_cutoff, 3, MIN_FREQ, bandpass.right_cutoff, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Cutoff right", &bandpass.right_cutoff, 3, bandpass.left_cutoff, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation left", &bandpass.left_attenuation, 1, 0, 100, "%.2f");
    modified |= ImGui::DragFloat("Attenuation right", &bandpass.right_attenuation, 1, 0, 100, "%.2f");

    if (modified)
        calculateBandpass();

    return modified;
}

bool FFT_EqualizerView::DrawRejector() {
    bool modified = setPreset(REJECTOR);
    ImGui::IdGuard ig(&rejector);
    modified |= ImGui::DragFloat("Cutoff left", &rejector.left_cutoff, 3, MIN_FREQ, rejector.right_cutoff, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Cutoff right", &rejector.right_cutoff, 3, rejector.left_cutoff, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation left", &rejector.left_attenuation, 1, 0, 100, "%.2f");
    modified |= ImGui::DragFloat("Attenuation right", &rejector.right_attenuation, 1, 0, 100, "%.2f");
    // modified |= ImGui::DragFloat("Attenuation right", &rejector.gain_db, 1, -100, 0, "%.2f");

    if (modified)
        calculateRejector();

    return modified;
}

bool FFT_EqualizerView::DrawKBand() {
    bool modified = setPreset(KBAND);
    ImGui::IdGuard ig(&kband);

    if (kband.bands.size() != kband.band_count || ImGui::DragInt("Number of bounds", &kband.band_count, 1, 3, 10)) {
        kband.bands.resize(kband.band_count);
        for (int i = 0; i < kband.band_count; i++) {
            kband.bands[i].freq_log = static_cast<float>(i) / (kband.band_count - 1);
        }
        modified = true;
    }

    for (int i = 0; i < kband.band_count; i++) {
        if (i > 0) ImGui::SameLine();

        ImGui::IdGuard ig(i);

        float freq = logFreqToNormal(kband.bands[i].freq_log);

        modified |= ImGui::VSliderFloat("##BandSlider", ImVec2(40, 200), &kband.bands[i].gain_db, -20, +20, "");

        if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            if (freq < 1000)
                ImGui::SetTooltip("%.0f Hz: %.2f db", freq, kband.bands[i].gain_db);
            else {
                ImGui::SetTooltip("%.1f Kz: %.2f db", freq / 1000, kband.bands[i].gain_db);
            }
        }

    }

    if (modified)
        calculateKBand();

    return modified;
}

/*
 Linear attenuation (db / decade) in double logarithimic scale
*/
static float interpolateAttenuation(float atten, float freq, float cutoff) {
    return std::pow(freq / cutoff, -atten / 20.0f);
}

void FFT_EqualizerView::setResponseSize(size_t size) {
    if (frequency_response.size() != size)
        frequency_response.resize(size, 1.0f);
}


std::vector<float> &FFT_EqualizerView::calculateLowpass() {
    size_t size = frequency_response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < lowpass.cutoff)
            frequency_response[i] = 1;
        else {
            frequency_response[i] = interpolateAttenuation(lowpass.attenuation, freq, lowpass.cutoff);
        }
    }
    return frequency_response;
}

std::vector<float> &FFT_EqualizerView::calculateHighpass() {
    size_t size = frequency_response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq > highpass.cutoff)
            frequency_response[i] = 1;
        else {
            frequency_response[i] = interpolateAttenuation(-highpass.attenuation, freq, highpass.cutoff);
        }
    }
    return frequency_response;
}

std::vector<float> &FFT_EqualizerView::calculateBandpass() {
    size_t size = frequency_response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < bandpass.left_cutoff) {
            frequency_response[i] =
                interpolateAttenuation(-bandpass.left_attenuation, freq, bandpass.left_cutoff);
        } else
        if (freq > bandpass.right_cutoff) {
            frequency_response[i] =
                interpolateAttenuation(bandpass.right_attenuation, freq, bandpass.right_cutoff);
        } else {
            frequency_response[i] = 1;
        }
    }
    return frequency_response;
}

std::vector<float> &FFT_EqualizerView::calculateRejector() {

    size_t size = frequency_response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < rejector.left_cutoff || freq > rejector.right_cutoff) {
            frequency_response[i] = 1;
        } else {
            frequency_response[i] = std::min(
                interpolateAttenuation(rejector.left_attenuation, freq, rejector.left_cutoff),
                interpolateAttenuation(-rejector.right_attenuation, freq, rejector.right_cutoff));

        }
    }
    return frequency_response;
}

std::vector<float> &FFT_EqualizerView::calculateKBand() {

    int left_band_idx = 0;
    float left_band_lfreq = kband.bands[left_band_idx].freq_log;
    float right_band_lfreq = kband.bands[left_band_idx + 1].freq_log;

    float gain_db_left  = kband.bands[left_band_idx].gain_db;
    float gain_db_right = kband.bands[left_band_idx + 1].gain_db;

    size_t size = frequency_response.size();
    for (int i = 0; i < size; i++) {
        float freq = std::max(MIN_FREQ, static_cast<float>(i) * MAX_FREQ / size);
        float log_freq = freqToLog(freq);

        if (log_freq > right_band_lfreq) {
            left_band_idx++;

            left_band_lfreq = kband.bands[left_band_idx].freq_log;
            right_band_lfreq = kband.bands[left_band_idx + 1].freq_log;

            gain_db_left  = kband.bands[left_band_idx].gain_db;
            gain_db_right = kband.bands[left_band_idx + 1].gain_db;
        }

        float k = (gain_db_right - gain_db_left) / (right_band_lfreq - left_band_lfreq);
        float gain_db = gain_db_left + k * (log_freq - left_band_lfreq);
        frequency_response[i] = dbToGain(gain_db);
    }
    return frequency_response;

}

void FFT_EqualizerView::DrawSettings() {
    // ImGui::Checkbox("On", enable);

    setResponseSize(eq->getSize());

    const char * const EQ_TABS = "EQ_TAB_BAR";
    bool modified = false;

     if (ImGui::BeginTabBar(EQ_TABS)) {
        if (ImGui::BeginTabItem("Lowpass")) {
            modified |= DrawLowpass();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Highpass")) {
            modified |= DrawHighpass();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bandpass")) {
            modified |= DrawBandpass();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rejector")) {
            modified |= DrawRejector();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("k-band")) {
            modified |= DrawKBand();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }


    if (modified) {
        log_freq_response = convertToDoubleLogScale(frequency_response, INNER_SAMPLE_RATE, frequency_response.size());
    }
    ImGui::Text("Response graph");
    if (useLogResponse) {
        ImGui::PlotLines("##Response", log_freq_response.data(), log_freq_response.size(),
            0, nullptr, -100, FLT_MAX, ImVec2(0, ImGui::GetFrameHeight() * 4));
    } else {
        float max_value =
            std::max(1.0f, *std::max_element(frequency_response.begin(), frequency_response.end()));
        ImGui::PlotLines("##Response", frequency_response.data(), frequency_response.size(),
            0, nullptr, 0, max_value, ImVec2(0, ImGui::GetFrameHeight() * 4));
    }

    ImGui::Checkbox("Log scale", &useLogResponse);

    if (ImGui::Button("Apply")) {
        saveAppliedPreset();

        eq->setFreqResponse(frequency_response);
    }


}

}
