#include "core/miniaudio_utils.h"
#include "miniaudio.h"
#include <memory>

void *getDeviceUserData(ma_device *pDevice) {
    return pDevice->pUserData;
}

void deinterleave_f32_frames(uint32_t channels, uint64_t frameCount, const float *interleaved, float** deinterleaved) {
    return ma_deinterleave_pcm_frames(ma_format_f32, channels, frameCount, interleaved, reinterpret_cast<void**>(deinterleaved));
}

void interleave_f32_frames(uint32_t channels, uint64_t frameCount, const float* const * deinterleaved, float *interleaved) {
    return ma_interleave_pcm_frames(ma_format_f32, channels, frameCount, 
        reinterpret_cast<const void**>(const_cast<const float **>(deinterleaved)), 
        interleaved);
}

// player


MaAudioPlayer::MaAudioPlayer(
    AudioFormat format, 
    uint32_t channels, uint32_t sampleRate, 
    device_data_proc_callback callback, void *userData
):
    audio_device_ptr(std::make_unique<ma_device>()) 
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format(format);
    config.playback.channels = channels;
    config.sampleRate = sampleRate;
    config.dataCallback = callback;
    config.pUserData = userData;

    if (ma_device_init(NULL, &config, audio_device_ptr.get()) != MA_SUCCESS) {
        PLOG_FATAL << "Failed to initialize audio engine";
        throw std::runtime_error("Audio init error");
    }

    if (ma_device_start(audio_device_ptr.get()) != MA_SUCCESS) {
        PLOG_FATAL << "Failed to start audio device";
        throw std::runtime_error("Audio init error");
    }

    PLOG_INFO << "Initialized ma_device " << audio_device_ptr.get();
}

void MaAudioPlayer::start() {
    ma_device_start(audio_device_ptr.get());
}

void MaAudioPlayer::stop() {
    ma_device_stop(audio_device_ptr.get());
}

MaAudioPlayer::~MaAudioPlayer() {
    ma_device_stop(audio_device_ptr.get());
    ma_device_uninit(audio_device_ptr.get());
    PLOG_INFO << "Unitialized ma_device " << audio_device_ptr.get();
}

// decoder

AudioDecoder::AudioDecoder(const std::string& path, int channels, int sampleRate):
    decoder_ptr(std::make_unique<ma_decoder>()), path_(path),
    out_channels_(channels), out_sampleRate_(sampleRate)
{
    ma_decoder_config dcd_cfg = ma_decoder_config_init(ma_format_f32, out_channels_, out_sampleRate_);
    ma_result init_res = ma_decoder_init_file(path.c_str(), &dcd_cfg, decoder_ptr.get());
    if (init_res != MA_SUCCESS) {
        PLOG_NONE << "Failed to decode file " << path;
        init = false;
    } else {
        init = true;
    }
}

std::optional<std::vector<audio_sample_t>> AudioDecoder::decode() {
    if (!init) {
        return std::nullopt;
    }

    ma_uint64 totalFrames = 0;

    ma_result len_res = ma_decoder_get_length_in_pcm_frames(decoder_ptr.get(), &totalFrames);
    if (len_res != MA_SUCCESS) {
        PLOG_WARNING << "Failed to get length of audio file";
    }

    std::vector<audio_sample_t> pcmData(totalFrames*out_channels_);

    // Reading
    ma_uint64 framesRead = 0;
    ma_result read_res = ma_decoder_read_pcm_frames(decoder_ptr.get(), pcmData.data(), totalFrames, &framesRead);

    if (framesRead < totalFrames) {
        PLOG_WARNING << "When decoding " << path_ << " read " << framesRead << "/" << totalFrames << " frames";
    }

    pcmData.resize(framesRead * out_channels_);

    return pcmData;
}

AudioDecoder::~AudioDecoder() {
    if (init) {
        ma_decoder_uninit(decoder_ptr.get());
    }
}