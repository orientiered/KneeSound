#include "core/playback_controller.h"
#include "core/audio_source.h"
#include "utils/buffer_utils.h"
#include "common.h"

namespace waves {

void PlaybackController::getFramesFromTimeline(void *out, uint32_t frameCount) {
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) <<
        "timeline callback: writing " << frameCount << " frames to " << out;

    if (!timeline_playing_) return;

    timeline.renderFrames(reinterpret_cast<audio_sample_t*>(out), timeline.playhead_frame.load(), frameCount);

    timeline.playhead_frame.fetch_add(frameCount);
}

void PlaybackController::getFramesFromPool(void* out, uint32_t frameCount) {
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) <<
        "pool callback: writing " << frameCount << " frames to " << out;

    if (!pool.getPlaying()) return;

    AudioSourcePtr cur = pool.getCurrentTrack();

    if (!cur) {
        return;
    }

    AudioBuffer &buf = cur->pcmData;
    int64_t cur_frame = pool.getCurrentFrame();

    const int64_t trackLen = buf.getFrameCount();

    int64_t start_idx = std::min(cur_frame, trackLen);
    int64_t end_idx   = std::min(cur_frame + frameCount, trackLen);

    audio_sample_t *fout = reinterpret_cast<audio_sample_t *>(out);
    for (int track_frame = start_idx, out_frame = 0; track_frame < end_idx; track_frame++, out_frame++) {
       for (int ch = 0; ch < INNER_CHANNELS; ch++) {
           fout[out_frame * INNER_CHANNELS + ch] = buf[ch][track_frame];
       }
    }

    pool.setCurrentTrackPosInFrames(std::min(trackLen, cur_frame + frameCount));
}

}
