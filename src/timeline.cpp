#include "timeline.h"

namespace waves {


/* ================= Clip     =================== */

// Renders frames to out array, ADDITIVELY 
// Doesn't write zeros
void Clip::renderFrames(std::vector<audio_sample_t> &out, ma_uint64 start_frame, ma_uint64 frame_count) {

    if (muted) return;

    ma_uint64 out_start_i = (start_frame > timeline_start_frame) ? 
                            0 : timeline_start_frame - start_frame;
    
    ma_uint64 out_end_i = ((start_frame + frame_count) > getTimelineEndFrame()) ? 
                            getTimelineEndFrame() - start_frame :
                            frame_count;

    if (out_start_i >= out_end_i) return;

    ma_uint64 src_i = *timelineToSourceFrame(start_frame + out_start_i);



    float gain = dbToGain(gain_db);
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) <<
        "Rendering clip " << name << ": si " << out_start_i << " ei " << out_end_i << " srci " << src_i << 
        " gain " << gain;
    //TODO: clip pan 
    for (ma_uint64 out_i = out_start_i; out_i < out_end_i; out_i++, src_i++) {

        for (int ch_idx = 0; ch_idx < INNER_CHANNELS; ch_idx++) {
            float sample = source->pcmData[src_i*INNER_CHANNELS + ch_idx];
            out[out_i*INNER_CHANNELS + ch_idx] += sample * gain;
        }

    }
    
}

/* ================= Track    =================== */

std::vector<audio_sample_t> &Track::renderFrames(ma_uint64 start_frame, ma_uint64 frame_count) {

    //! LOOKS LIKE TERRIBLE IDEA
    if (frame_count * INNER_CHANNELS > rendering_buffer.size()) {
        rendering_buffer.resize(frame_count * INNER_CHANNELS);
    }
    
    // clearing buffer
    std::fill(rendering_buffer.begin(), rendering_buffer.begin() + frame_count * INNER_CHANNELS, 0);

    // early out when track is muted
    if (mute) return rendering_buffer;

    // rendering clips
    for (int clip_idx = 0; clip_idx < clips.size(); clip_idx++) {
        Clip& clip = clips[clip_idx];
        clip.renderFrames(rendering_buffer, start_frame, frame_count);
    }

    // applying effects (gain)
    float gain = dbToGain(gain_db);

    for (int i = 0; i < frame_count * INNER_CHANNELS; i++) {
        rendering_buffer[i] *= gain;
        // PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << "track amp: " << rendering_buffer[i]*gain;

    }

    return rendering_buffer;
}


/* ================= Timeline =================== */

std::vector<audio_sample_t>& TimeLine::renderFrames(ma_uint64 start_frame, ma_uint64 frame_count) {

    //! LOOKS LIKE TERRIBLE IDEA
    if (frame_count * INNER_CHANNELS > rendering_buffer.size()) {
        rendering_buffer.resize(frame_count * INNER_CHANNELS);
    }

    // clearing buffer
    std::fill(rendering_buffer.begin(), rendering_buffer.begin() + frame_count * INNER_CHANNELS, 0);


    float gain = dbToGain(gain_db);


    for (int track_idx = 0; track_idx < tracks.size(); track_idx++) {
        auto &buffer = tracks[track_idx].renderFrames(start_frame, frame_count);
        for (int i = 0; i < frame_count * INNER_CHANNELS; i++) {
            rendering_buffer[i] += buffer[i] * gain;
            PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << "timeline_amp: "<< rendering_buffer[i] <<
                                                            " track_amp: " << buffer[i];
        }   
    }
    

    return rendering_buffer;
}


bool TimeLine::isValidClipId(ClipId_t id) {
    return getTrackAndClipIdx(id) ? true: false;
}

std::optional<ClipLoc> TimeLine::getTrackAndClipIdx(ClipId_t id) {
    for (size_t track_idx = 0; track_idx < tracks.size(); track_idx++) {
        for (size_t clip_idx = 0; clip_idx < tracks[track_idx].clips.size(); clip_idx++) {
            if (tracks[track_idx].clips[clip_idx].id == id) {
                return ClipLoc{track_idx, clip_idx};
            }
        }
    }

    return std::nullopt;
}

Clip *TimeLine::getClipById(ClipId_t id) {
    auto loc = getTrackAndClipIdx(id);
    if (loc)
        return &tracks[loc->track_idx].clips[loc->clip_idx];

    return nullptr;
}

void TimeLine::removeClipById(ClipId_t id) {

    for (Track& track: tracks) {
        for (auto clip_it = track.clips.begin(); clip_it != track.clips.end(); clip_it++ ) {
            if (clip_it -> id == id) {
                track.clips.erase(clip_it);
                return;
            }
        }
    }
}

std::optional<size_t> TimeLine::getTrackIdx(ClipId_t id) {
    auto loc = getTrackAndClipIdx(id);
    if (loc) return loc->track_idx;
    else return std::nullopt;
}

void TimeLine::moveClipToTrack(ClipId_t id, int track_idx) {
    if (track_idx < 0) return;

    auto loc = getTrackAndClipIdx(id);
    if (!loc || loc->track_idx == track_idx) return;
    auto [track_i, clip_i] = *loc;

    if (track_idx >= tracks.size())
        tracks.resize(track_idx+1);
    
    std::vector<Clip>& old_clips = tracks[track_i].clips;

    tracks[track_idx].addClip(old_clips[clip_i]);

    old_clips.erase(old_clips.begin() + clip_i);

}

ClipId_t TimeLine::addClip(const Clip& clip, int track_idx) {
    if (track_idx < 0) return CLIP_NONE;
    if (track_idx >= tracks.size())
        tracks.resize(track_idx+1);

    tracks[track_idx].addClip(clip);

    return clip.id;
}


}

