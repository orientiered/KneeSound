#include <algorithm>
#include <memory>
#include <vector>

#include "common.h"
#include "serialization.h"
#include "effects/audio_effects.h"


namespace waves {

/* ================= EFFECT CHAIN ================== */

size_t EffectChain::getLatency() {
    size_t latency = 0;

    EffectChain::ChainPtr chain_ptr = getChain();

    for (std::shared_ptr<EffectSlot> effect : *chain_ptr) {
        latency += effect->kernel->getLatencySamples();
    }

    return latency;
}


void EffectChain::modify(std::function<void(Chain&)> fn) {
    ChainPtr old = getChain();
    ChainPtr next = std::make_shared<Chain>();
    next->reserve(old->size() + 1);
    for (std::shared_ptr<EffectSlot> slot : *old) {
        next->push_back(slot);
    }
    fn(*next);
    chain_ptr_.store(next, std::memory_order_release);
}

void EffectChain::processBlock(AudioBuffer &in_out) {
    // ensuring that chain won't be deleted
    EffectChain::ChainPtr chain_ptr = getChain();

    for (std::shared_ptr<EffectSlot> effect : *chain_ptr) {
        if (effect && effect->kernel)
            effect->kernel->process(in_out, in_out);
    }

}


void EffectChain::serialize(ProjectWriter output) const {
    auto effect_chain = getChain();
    output.array("chain");
    for (auto effect_ptr: *effect_chain) {
        ProjectWriter effect_writer = output.push_back("chain");
        effect_writer.write("id", effect_ptr->id);
        effect_ptr->view->serialize(effect_writer);
    }
}

void EffectChain::deserialize(ProjectReader input) {
    std::size_t cnt = input.arr_size("chain");

    PluginManager *pm = input.getCtx().plugin_manager;
    ChainPtr new_chain = std::make_shared<Chain>();
    for (std::size_t idx = 0; idx < cnt; idx++) {
        ProjectReader effct_reader = input.read_array("chain", idx);
        EffectId id = effct_reader.read("id", std::string("UNKNOWN_EFFECT"));
        auto new_effect_slot = pm->buildEffect(id);
        if (new_effect_slot)
            new_effect_slot->view->deserialize(effct_reader);

        new_chain->push_back(new_effect_slot);
    }    

    chain_ptr_.store(new_chain);
}

/* ====== EFFECT CONSTRUCTION ======================= */

void PluginManager::updateDescriptors() {
    descriptors_.clear();

    for (auto &factory: factories_) {
        if (factory)
            descriptors_.push_back(factory->getDescriptor());
    }
}

std::shared_ptr<EffectSlot> PluginManager::buildEffect(const EffectId &id) {
    PLOG_DEBUG << "Building plugin with id " << id;

    auto desc_it = std::find_if(descriptors_.begin(), descriptors_.end(),
        [&id](const EffectDescriptor& desc) {return desc.id == id;} );

    if (desc_it == descriptors_.end()) {
        PLOG_DEBUG << "Plugin not found";
        return nullptr;
    }

    size_t factory_idx = desc_it - descriptors_.begin();

    std::shared_ptr<EffectSlot> result = std::make_shared<EffectSlot>(factories_[factory_idx]->build());
    result->id = desc_it->id;
    result->name = desc_it->name;

    PLOG_DEBUG << "Created plugin " << desc_it->name << " (id:" << id << ")";
    return result;
}

/* ================= FFT PIPELINE ================== */
void FreqDomainPipeline::prepareTimeData(const AudioBuffer &in, size_t ch_idx) {
    // time_data: | previous block | data |
    // 2*hop_size     hop_size      hop_size
    audio_sample_t *prev_block = previous_block_.getChannel(ch_idx);
    const audio_sample_t *cur_block = in[ch_idx];

    std::copy_n(prev_block, hop_size_, time_data.begin());

    // saving current block for next call
    std::copy_n(cur_block, hop_size_, prev_block);

    // filling time_data for fft
    std::copy_n(cur_block, hop_size_, time_data.begin() + hop_size_);

}

void FreqDomainPipeline::applyOverlap(AudioBuffer &out, size_t ch_idx) {

    audio_sample_t *overlap = overlap_add_.getChannel(ch_idx);
    audio_sample_t *out_ch = out.getChannel(ch_idx);

    for (size_t idx = 0; idx < hop_size_; idx++) {
        // output[i] = overlapp_add[i] + time_data[i], i = 0...hop-1
        out_ch[idx] = time_data[idx] + overlap[idx];
    }

    // overlapp_add[i] = time_data[i], i = hop...2*hop-1
    std::copy_n(time_data.begin() + hop_size_, hop_size_, overlap);
}

// void PitchShifter::operator()(std::vector<kiss_fft_cpx> &freq_data) {
//     const size_t size = freq_data.size();

//     auto getInterpolatedFreq = [&](float idx) -> kiss_fft_cpx {
//         if (idx < 0 || idx >= size) return {0, 0};
//         int left_idx = std::floor(idx);
//         float p = idx - left_idx;

//         kiss_fft_cpx left = freq_data[left_idx];
//         kiss_fft_cpx right = (left_idx < (size - 1)) ? freq_data[left_idx+1] :
//                                                        kiss_fft_cpx{0, 0};
//         kiss_fft_cpx result = {
//             left.r * (1-p) + right.r * p,
//             left.i * (1-p) + right.i * p
//         };
//         return result;
//     };

//     if (stretch_k > 1) {
//         for (int i = 0; i < size; i++) {
//             freq_data[i] = getInterpolatedFreq(i    *stretch_k);
//         }
//     } else {
//         for (int i = size-1; i >= 0; i--) {
//             freq_data[i] = getInterpolatedFreq(i*stretch_k);
//         }
//     }
// }

}
