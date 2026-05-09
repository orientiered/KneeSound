#pragma once

#include "common.h"

#include "miniaudio.h"

class MaAudioPlayer {
private:
    ma_device audio_device;

public:

    MaAudioPlayer(ma_format format, ma_uint32 channels, ma_uint32 sampleRate, ma_device_data_proc callback, void *userData) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = format;
        config.playback.channels = channels;
        config.sampleRate = sampleRate;
        config.dataCallback = callback;
        config.pUserData = userData;

        if (ma_device_init(NULL, &config, &audio_device) != MA_SUCCESS) {
            PLOG_FATAL << "Failed to initialize audio engine";
            throw std::runtime_error("Audio init error");
        }

        if (ma_device_start(&audio_device) != MA_SUCCESS) {
            PLOG_FATAL << "Failed to start audio device";
            throw std::runtime_error("Audio init error");
        }

        PLOG_INFO << "Initialized ma_device " << &audio_device;
    }

    void start() {
        ma_device_start(&audio_device);
    }

    void stop() {
        ma_device_stop(&audio_device);
    }

    ~MaAudioPlayer() {
        ma_device_stop(&audio_device);
        ma_device_uninit(&audio_device);
        PLOG_INFO << "Unitialized ma_device " << &audio_device;
    }

};


class AudioDecoder {
    ma_decoder decoder;
    bool init = false;

    std::string path_;
    int out_channels_, out_sampleRate_;

public:
    AudioDecoder(const std::string& path, int channels = waves::INNER_CHANNELS, int sampleRate = waves::INNER_SAMPLE_RATE):
        out_channels_(channels), out_sampleRate_(sampleRate)
    {
        path_ = path;

        ma_decoder_config dcd_cfg = ma_decoder_config_init(ma_format_f32, out_channels_, out_sampleRate_);
        ma_result init_res = ma_decoder_init_file(path.c_str(), &dcd_cfg, &decoder);
        if (init_res != MA_SUCCESS) {
            PLOG_NONE << "Failed to decode file " << path;
            init = false;
        } else {
            init = true;
        }
    }

    std::optional<std::vector<audio_sample_t>> decode() {
        if (!init) {
            return std::nullopt;
        }

        ma_uint64 totalFrames = 0;

        ma_result len_res = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        if (len_res != MA_SUCCESS) {
            PLOG_WARNING << "Failed to get length of audio file";
        }

        std::vector<audio_sample_t> pcmData(totalFrames*out_channels_);

        // Reading
        ma_uint64 framesRead = 0;
        ma_result read_res = ma_decoder_read_pcm_frames(&decoder, pcmData.data(), totalFrames, &framesRead);

        if (framesRead < totalFrames) {
            PLOG_WARNING << "When decoding " << path_ << " read " << framesRead << "/" << totalFrames << " frames";
        }

        pcmData.resize(framesRead * out_channels_);

        return pcmData;
    }

    ~AudioDecoder() {
        if (init) {
            ma_decoder_uninit(&decoder);
        }
    }
};
