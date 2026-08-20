#pragma once

#include <atomic>

#include "miniaudio.h"
#include "utils/buffer_utils.h"

namespace waves {

struct PeakCache {
    uint64_t block_size_;
    struct min_max {
        float min = 10.0f;
        float max = -10.0f;
        void update(float s) {
            min = std::min(min, s);
            max = std::max(max, s);
        }
        void update(const min_max& other) {
            min = std::min(min, other.min);
            max = std::max(max, other.max);
        }
    };

    std::vector<min_max> peaks; // min and max in block

    PeakCache(uint64_t block_size, const AudioBuffer &samples);
    PeakCache(uint64_t block_size, const PeakCache &cache);
};

struct PeakCacheManager {
    std::vector<PeakCache> peak_caches;
    void build(const AudioBuffer &samples) {
        if (!peak_caches.empty()) return;

        peak_caches.emplace_back(16, samples);
        peak_caches.emplace_back(64, peak_caches.back());
        peak_caches.emplace_back(256, peak_caches.back());
        peak_caches.emplace_back(1024, peak_caches.back());
        peak_caches.emplace_back(4096, peak_caches.back());

        std::reverse(peak_caches.begin(), peak_caches.end());
    }

    std::optional<PeakCache::min_max> getPeak(uint64_t f_start, uint64_t f_end) const;
};

struct AudioSource {
    bool valid = false;
    std::string name;
    std::string path; // used as unique identifier when exporting/importing projects

    std::atomic<bool> loading = false; // use when loading asynchronously

    MultiChannelBuffer pcmData;

    PeakCacheManager cache;

    AudioSource(const std::string& name_, const std::string& path_): name(name_), path(path_) {}

    float getMonoSampleAmplitude(uint64_t frame) const {
        return pcmData.getMeanSample(frame);
    }

    PeakCache::min_max getPeakFallback(uint64_t start, uint64_t end) const {
        PeakCache::min_max result;
        end = std::min(end, getDurationFrames());
        for (uint64_t f = start; f < end; ++f) {
            result.update(getMonoSampleAmplitude(f));
        }
        return result;
    }

    PeakCache::min_max getPeak(uint64_t start, uint64_t end) const {
        auto cached = cache.getPeak(start, end);
        if (cached) return *cached;

        return getPeakFallback(start, end);
    }


    uint64_t getDurationFrames() const {
        return pcmData.getFrameCount();
    }

};

using AudioSourcePtr = std::shared_ptr<AudioSource>;


}
