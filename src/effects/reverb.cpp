#include <atomic>
#include <memory>

#include "common.h"
#include "imgui.h"
#include "effects/reverb.h"
#include "utils/buffer_utils.h"

namespace waves {

void ReverbKernel::prepare(uint32_t delay, uint32_t block_size, uint32_t channels) {
    std::shared_ptr<state_t> new_state = std::make_shared<state_t>(channels,
        BulkQueue<audio_sample_t>((block_size + delay)*2));

    // TODO: race conditions
    wet_.resize(block_size);

    for (int ch = 0; ch < channels; ch++) {
        (*new_state)[ch].fill_bulk(audio_sample_t(), delay+block_size);
    }

    state_.store(new_state, std::memory_order_release);
    delay_frames_ = delay;
}


void ReverbKernel::process(const AudioBuffer& in, AudioBuffer &out) {
    const size_t numSamples = in.getFrameCount();

    std::shared_ptr<state_t> state = state_.load();

    for (size_t ch = 0; ch < in.getChannels(); ch++) {
        const audio_sample_t *ch_in = in[ch];
        audio_sample_t *ch_out = out.getChannel(ch);

        // retrieving reverb
        (*state)[ch].pop_bulk(wet_.data(), numSamples);

        for (size_t i = 0; i < numSamples; ++i) {
            ch_out[i] = dry_gain_ * ch_in[i] + wet_gain_ * wet_[i];
        }

        // storing samples to queue
        (*state)[ch].push_bulk(ch_out, numSamples);


    }
}

void ReverbView::DrawSettings() {
    ImGui::SeparatorText("Simple reverb");
    ImGui::DragFloat("Dry gain", &reverb_->getDryGain(), 0.01, 0, 1);
    ImGui::DragFloat("Wet gain", &reverb_->getWetGain(), 0.01, 0, 1);

    delay_sec_ = static_cast<float>(reverb_->getDelay()) / INNER_SAMPLE_RATE;

    if (ImGui::DragFloat("Delay", &delay_sec_, 0.05, 0, 5)) {
        reverb_->prepare(delay_sec_ * INNER_SAMPLE_RATE);
    }

}

void ReverbView::serialize(ProjectWriter output) const {
    output.write("dry_gain", reverb_->getDryGain());
    output.write("wet_gain", reverb_->getWetGain());
    output.write("delay_sec", delay_sec_);
}

void ReverbView::deserialize(ProjectReader input) {
    reverb_->getDryGain() = input.read("dry_gain", 0.5f);
    reverb_->getWetGain() = input.read("wet_gain", 0.5f);

    delay_sec_ = input.read("delay_sec", 0.f);
    reverb_->prepare(delay_sec_ * INNER_SAMPLE_RATE);
}

}
