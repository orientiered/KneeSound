#pragma once

#include "common.h"
#include "effects/audio_effects.h"
#include "utils/buffer_utils.h"
#include <cstdint>

namespace waves {

class ReverbKernel : public IDspKernel {
private:
    float gain_ = 0.3f;
    uint32_t delay_frames_ = 3000;

    using state_t = std::vector<BulkQueue<audio_sample_t>>;
    std::atomic<std::shared_ptr<state_t>> state_;

    std::vector<audio_sample_t> temp_;
public:

    ReverbKernel() {}

    void prepare(uint32_t delay = 3000, uint32_t block_size = RENDER_BLOCK_SIZE, uint32_t channels = INNER_CHANNELS);
    void process(const AudioBuffer& in, AudioBuffer &out) override;

    uint32_t getLatencySamples() const override {
        return 0;
    }

    void reset() override {
        // state.clear()
        // std::fill(states.begin(), states.end(), FilterState());
    }

    uint32_t getDelay() const { return delay_frames_; }
    float &getGain() { return gain_; }

    ~ReverbKernel() override = default;
};


class ReverbView : public IEffectView {
private:
    float delay_sec_ = 0;
public:
    void DrawSettings() override;
    ReverbView(ReverbKernel *reverb): reverb_(reverb) {
    }

    ~ReverbView() override = default;
private:
    ReverbKernel *reverb_ = nullptr;
};

class ReverbFactory: public IEffectFactory {
public:
    ~ReverbFactory() override = default;
    EffectDescriptor getDescriptor() override {
        return {
            .name = "Reverb",
            .version = "0.1",
            .id = "knee_reverb"
        };
    }

    PluginPair build() override {
        std::unique_ptr<ReverbKernel> kernel = std::make_unique<ReverbKernel>();
        std::unique_ptr<IEffectView> view = std::make_unique<ReverbView>(kernel.get());

        kernel->prepare();

        return {std::move(kernel), std::move(view)};
    }
};


}
