#include "timeline.h"

namespace waves {


/* ================= Clip     =================== */

// Renders frames to out array, ADDITIVELY 
// Doesn't write zeros

std::ostream& operator<<(std::ostream& os, const Clip& clip) {
    os << "Clip '" << clip.name << "'[" << &clip << "][id:" << clip.id << "] dump:\n"
       << "audio source " << clip.source << "\n"
       << "Source boundaries: [" << clip.source_start_frame << ", " << clip.source_end_frame << ")\n"
       << "Timeline start frame: " << clip.timeline_start_frame << "\n"
       << "Gain: " << clip.gain_db << " Muted: " << clip.muted << " Pan: " << clip.pan; 
    return os;
}

void Clip::renderFrames(std::vector<audio_sample_t> &out, ma_uint64 start_frame, ma_uint64 frame_count) {

    if (muted) return;

    ma_int64 out_start_i = (start_frame >= timeline_start_frame) ? 
                            0 : timeline_start_frame - start_frame;
    
    ma_int64 out_end_i = ((start_frame + frame_count) > getTimelineEndFrame()) ? 
                            getTimelineEndFrame() - start_frame :
                            frame_count;

    if (out_start_i >= out_end_i) return;

    auto src_i_opt = timelineToSourceFrame(start_frame + out_start_i);
    if (!src_i_opt) {
        PLOG_ERROR << "Invalid source start frame index";
        PLOG_ERROR << *this << "\n"
                   << out_start_i << " " << out_end_i << "\n";
    }
    ma_uint64 src_i = *src_i_opt;



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

std::optional<Clip> Clip::cut(ma_uint64 timeline_pos) {
    PLOG_DEBUG << "Cutting clip " << id << " on pos " << timeline_pos;
    std::optional<ma_uint64> source_pos = timelineToSourceFrame(timeline_pos);
    // if cut position is not in clip, do not cut
    if (!source_pos) return std::nullopt;

    Clip new_clip = copy();
    new_clip.timeline_start_frame = timeline_pos;
    new_clip.source_end_frame = source_end_frame;
    new_clip.source_start_frame = *source_pos;

    source_end_frame = *source_pos;

    return new_clip;
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

/* ================= Clipboard ================== */

void TimeLine::copyToClipboard(ClipId_t id) {
    PLOG_DEBUG << "Copying clip to clipboard " << id;

    Clip *clip = getClipById(id);
    if (!clip) return;

    clipboard.data = clip->copy(); 
}

void TimeLine::cutToClipboard(ClipId_t id) {
    PLOG_DEBUG << "Cutting clip to clipboard " << id;

    Clip *clip = getClipById(id);
    if (!clip) return;

    // copying without changing id
    clipboard.data = *clip; 

    removeClipById(id); // removing clip 

}

std::optional<Clip> TimeLine::pasteFromClipboard() {
    if (!clipboard.data) return std::nullopt;
    // always copying
    return clipboard.data->copy();
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


void TimeLine::removeClipByLoc(ClipLoc loc) {
    // invalid loc
    if (tracks.size() <= loc.track_idx) {
        return;
    }

    if (tracks[loc.track_idx].clips.size() <= loc.clip_idx) {
        return;
    }

    // Synchonized deletion
    mtx.lock();

    std::vector<Clip> &clips = tracks[loc.track_idx].clips;
    clips.erase(clips.begin() + loc.clip_idx);

    mtx.unlock();
}


void TimeLine::removeClipById(ClipId_t id) {

    auto clipLoc = getTrackAndClipIdx(id);
    if (clipLoc) return removeClipByLoc(*clipLoc);

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

    removeClipByLoc(*loc);
}


ClipId_t TimeLine::addClip(const Clip& clip, int track_idx) {
    if (track_idx < 0) return CLIP_NONE;
    if (track_idx >= tracks.size())
        tracks.resize(track_idx+1);

    tracks[track_idx].addClip(clip);

    return clip.id;
}


}

