#include <thread>

#include "core/audio_source.h"
#include "core/miniaudio_utils.h"

namespace waves {

AudioSourcePtr decode_audio_from_file(const std::string& name, const std::string& path, bool async) {

    PLOG_INFO << "Decoding audio from file " << path << " (name '" << name << "')";
    PLOG_INFO << "Async: " << async;

    AudioDecoder decoder(path);

    AudioSourcePtr result = std::make_shared<AudioSource>(name, path);

    auto decode = [](AudioSourcePtr source) {
        AudioDecoder decoder(source->path);

        // busy flag
        source->loading.store(true);

        // trying to decode
        std::optional<std::vector<audio_sample_t>> pcmData = decoder.decode();

        // on success setting valid flag and computing peaks cache
        if (pcmData) {
            uint64_t frameCount = pcmData->size() / INNER_CHANNELS;
            source->pcmData = AudioBuffer(frameCount, INNER_CHANNELS);
            ma_deinterleave_pcm_frames(ma_format_f32, INNER_CHANNELS, frameCount,
                pcmData->data(), reinterpret_cast<void**>(source->pcmData.data()));

            source->valid = true;
            PLOG_INFO << "Building peaks cache...";
            source->cache.build(source->pcmData);
        }

        // not busy
        source->loading.store(false);
    };

    if (async) {
        std::thread decoder_thread(decode, result);
        decoder_thread.detach();
    } else {
        decode(result);
    }

    return result;
}

}
