#include "playback_controller.h"

namespace waves {

void PlaybackController::getFramesFromTimeline(void *out, ma_uint32 frameCount) {
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << 
        "timeline callback: writing " << frameCount << " frames to " << out;

    timeline.renderFrames(reinterpret_cast<audio_sample_t*>(out), timeline.playhead_frame, frameCount);

    timeline.playhead_frame.fetch_add(frameCount);
}

void PlaybackController::getFramesFromPool(void* out, ma_uint32 frameCount) { 
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << 
        "pool callback: writing " << frameCount << " frames to " << out;

    const std::vector<float>& pcmData = (*currentTrack)->pcmData;
    const size_t trackLen = pcmData.size();
    const int64_t trackLenInFrames = trackLen / INNER_CHANNELS;

    auto startIt = (INNER_CHANNELS*currentFrame >= trackLen ) ?
                    pcmData.end() :
                    pcmData.begin() + INNER_CHANNELS*currentFrame;
    auto endIt = (INNER_CHANNELS*(currentFrame + frameCount) >= trackLen) ?
                    pcmData.end() :
                    pcmData.begin() + INNER_CHANNELS*(currentFrame + frameCount);

    std::copy(startIt, endIt, reinterpret_cast<float*>(out));
    
    currentFrame = std::min(trackLenInFrames, currentFrame +frameCount);
}

}