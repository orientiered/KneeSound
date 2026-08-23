#include "common.h"
#include "effects/fft_equalizer.h"

#include "imgui.h"
#include "imgui_misc.h"

#include "serialization.h"
#include "utils/misc_utils.h"

namespace waves {

using namespace fft_detail;
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

float logFreqToNormal(float freq_log) {
    return MIN_FREQ * std::pow(MAX_FREQ / MIN_FREQ, freq_log);
}
float freqToLog(float freq) {
    return std::log(freq / MIN_FREQ) / std::log(MAX_FREQ / MIN_FREQ);
}

// ========= Presets ==========
bool FFT_Lowpass::Draw() {
    ImGui::IdGuard ig(this);
    bool modified = false;
    modified |= ImGui::DragFloat("Cutoff", &cutoff, 3, MIN_FREQ, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation", &attenuation, 1, 0, 100, "%.2f");

    return modified;
}

void FFT_Lowpass::calculate(std::vector<float> &response) {
    size_t size = response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < cutoff)
            response[i] = 1;
        else {
            response[i] = interpolateAttenuation(attenuation, freq, cutoff);
        }
    }
}

// ============ 
bool FFT_Highpass::Draw() {
    ImGui::IdGuard ig(this);
    bool modified = false;
    modified |= ImGui::DragFloat("Cutoff", &cutoff, 3, MIN_FREQ, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation", &attenuation, 1, 0, 100, "%.2f");

    return modified;
}

void FFT_Highpass::calculate(std::vector<float> &response) {
    size_t size = response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq > cutoff)
            response[i] = 1;
        else {
            response[i] = interpolateAttenuation(-attenuation, freq, cutoff);
        }
    }
}

// =======

bool FFT_Bandpass::Draw() {
    ImGui::IdGuard ig(this);
    bool modified = false;
    modified |= ImGui::DragFloat("Cutoff left", &left_cutoff, 3, MIN_FREQ, right_cutoff, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Cutoff right", &right_cutoff, 3, left_cutoff, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation left", &left_attenuation, 1, 0, 100, "%.2f");
    modified |= ImGui::DragFloat("Attenuation right", &right_attenuation, 1, 0, 100, "%.2f");

    return modified;
}

void FFT_Bandpass::calculate(std::vector<float> &response) {
    size_t size = response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < left_cutoff) {
            response[i] =
                interpolateAttenuation(-left_attenuation, freq, left_cutoff);
        } else
        if (freq > right_cutoff) {
            response[i] =
                interpolateAttenuation(right_attenuation, freq, right_cutoff);
        } else {
            response[i] = 1;
        }
    }
}

// ======
bool FFT_Rejector::Draw() {
    ImGui::IdGuard ig(this);
    bool modified = false;
    modified |= ImGui::DragFloat("Cutoff left", &left_cutoff, 3, MIN_FREQ, right_cutoff, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Cutoff right", &right_cutoff, 3, left_cutoff, MAX_FREQ, "%.2f", ImGuiSliderFlags_Logarithmic);
    modified |= ImGui::DragFloat("Attenuation left", &left_attenuation, 1, 0, 100, "%.2f");
    modified |= ImGui::DragFloat("Attenuation right", &right_attenuation, 1, 0, 100, "%.2f");

    return modified;
}

void FFT_Rejector::calculate(std::vector<float> &response) {
    size_t size = response.size();
    for (int i = 0; i < size; i++) {
        float freq = static_cast<float>(i) * MAX_FREQ / size;
        if (freq < left_cutoff || freq > right_cutoff) {
            response[i] = 1;
        } else {
            response[i] = std::min(
                interpolateAttenuation(left_attenuation, freq, left_cutoff),
                interpolateAttenuation(-right_attenuation, freq, right_cutoff));
        }
    }
}

// =====
bool FFT_KBand::Draw() {
    ImGui::IdGuard ig(this);

    bool modified = false;
    if (bands.size() != band_count || ImGui::DragInt("Number of bounds", &band_count, 1, 3, 10)) {
        bands.resize(band_count);
        for (int i = 0; i < band_count; i++) {
            bands[i].freq_log = static_cast<float>(i) / (band_count - 1);
        }
        modified = true;
    }

    for (int i = 0; i < band_count; i++) {
        if (i > 0) ImGui::SameLine();

        ImGui::IdGuard ig(i);

        float freq = logFreqToNormal(bands[i].freq_log);

        modified |= ImGui::VSliderFloat("##BandSlider", ImVec2(40, 200), &bands[i].gain_db, -20, +20, "");

        if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            if (freq < 1000)
                ImGui::SetTooltip("%.0f Hz: %.2f db", freq, bands[i].gain_db);
            else {
                ImGui::SetTooltip("%.1f Kz: %.2f db", freq / 1000, bands[i].gain_db);
            }
        }

    }

    return modified;
}

void FFT_KBand::calculate(std::vector<float> &response) {

    int left_band_idx = 0;
    float left_band_lfreq = bands[left_band_idx].freq_log;
    float right_band_lfreq = bands[left_band_idx + 1].freq_log;

    float gain_db_left  = bands[left_band_idx].gain_db;
    float gain_db_right = bands[left_band_idx + 1].gain_db;

    size_t size = response.size();
    for (int i = 0; i < size; i++) {
        float freq = std::max(MIN_FREQ, static_cast<float>(i) * MAX_FREQ / size);
        float log_freq = freqToLog(freq);

        if (log_freq > right_band_lfreq) {
            left_band_idx++;

            left_band_lfreq = bands[left_band_idx].freq_log;
            right_band_lfreq = bands[left_band_idx + 1].freq_log;

            gain_db_left  = bands[left_band_idx].gain_db;
            gain_db_right = bands[left_band_idx + 1].gain_db;
        }

        float k = (gain_db_right - gain_db_left) / (right_band_lfreq - left_band_lfreq);
        float gain_db = gain_db_left + k * (log_freq - left_band_lfreq);
        response[i] = dbToGain(gain_db);
    }
}

void FFT_EqualizerView::DrawSettings() {
    // ImGui::Checkbox("On", enable);

    setResponseSize(eq->getSize());

    const char * const EQ_TABS = "EQ_TAB_BAR";
    bool modified = false;

    if (ImGui::BeginTabBar(EQ_TABS)) {
        for (int i = 0; i < presets.size(); i++) {
            PresetClass& preset = presets[i];
            if (ImGui::BeginTabItem(preset.name().c_str())) {
                modified |= setPreset(i);
                modified |= preset.Draw();
                
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }


    if (modified) {
        presets[preset].calculate(frequency_response);
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

void FFT_EqualizerView::serialize(ProjectWriter output) const {
    SERIALIZE_SIMPLE(output, useLogResponse);
    
    for (const PresetClass& preset: presets) {
        preset.serialize(output.nest(preset.name()));
    }
}

void FFT_EqualizerView::deserialize(ProjectReader input) {
    DESERIALIZE_OPT(input, useLogResponse);
    
    for (PresetClass& preset: presets) {
        if (auto preset_input = input.nest(preset.name())) {
            preset.deserialize(*preset_input);
        }
    }
}

}
