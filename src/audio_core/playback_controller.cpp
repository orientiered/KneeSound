#include "core/playback_controller.h"
#include "utils/buffer_utils.h"
#include "common.h"

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

    AudioBuffer &buf = (*currentTrack)->pcmData;

    const int64_t trackLen = buf.getFrameCount();

    int64_t start_idx = std::min(currentFrame, trackLen);
    int64_t end_idx   = std::min(currentFrame + frameCount, trackLen);

    audio_sample_t *fout = reinterpret_cast<audio_sample_t *>(out);
    for (int track_frame = start_idx, out_frame = 0; track_frame < end_idx; track_frame++, out_frame++) {
       for (int ch = 0; ch < INNER_CHANNELS; ch++) {
           fout[out_frame * INNER_CHANNELS + ch] = buf[ch][track_frame];
       }
   }
    currentFrame = std::min(trackLen, currentFrame +frameCount);
}

}
