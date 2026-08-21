#pragma once

#include "common.h"

#include <cstdint>
#include <memory>

// forward declaration
struct ma_device;
struct ma_decoder;

using device_data_proc_callback = void(*)(ma_device *pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
void *getDeviceUserData(ma_device *pDevice);

void deinterleave_f32_frames(uint32_t channels, uint64_t frameCount, const float *interleaved, float** deinterleaved);
void interleave_f32_frames(uint32_t channels, uint64_t frameCount, const float* const * deinterleaved, float *interleaved);

// Matches ma_format
enum class AudioFormat {
    unknown = 0,
    s16 = 2,
    f32 = 5
};

class MaAudioPlayer {
private:
    std::unique_ptr<ma_device> audio_device_ptr;

public:
    MaAudioPlayer(AudioFormat format, uint32_t channels, uint32_t sampleRate, device_data_proc_callback callback, void *userData);

    void start();
    void stop();
    ~MaAudioPlayer();

};

class AudioDecoder {
    std::unique_ptr<ma_decoder> decoder_ptr;
    bool init = false;

    std::string path_;
    int out_channels_, out_sampleRate_;

public:
    AudioDecoder(const std::string& path, int channels = waves::INNER_CHANNELS, int sampleRate = waves::INNER_SAMPLE_RATE);
    std::optional<std::vector<audio_sample_t>> decode();
    ~AudioDecoder();
};
