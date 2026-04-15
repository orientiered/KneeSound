#include "timeline.h"

namespace waves {

/* ================= Clip     =================== */


std::ostream& operator<<(std::ostream& os, const Clip& clip) {
    os << "Clip '" << clip.name << "'[" << &clip << "][id:" << clip.id << "] dump:\n"
       << "audio source " << clip.source << "\n"
       << "Source boundaries: [" << clip.source_start_frame << ", " << clip.source_end_frame << ")\n"
       << "Timeline start frame: " << clip.timeline_start_frame << "\n"
       << "Gain: " << clip.gain_db << " Muted: " << clip.muted << " Pan: " << clip.pan; 
    return os;
}

// Renders frames to out array, ADDITIVELY 
// Doesn't write zeros
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

    // TODO: clip pan 
    auto process_frame = [&](float *in, float *out) {
        float left = in[0], right = in[1];

        float pan_left  = (pan <= 0) ? 1 : (1 - pan);
        float pan_right = (pan >= 0) ? 1 : (1 + pan); 
        float out_left  = left  * gain * pan_left;
        float out_right = right * gain * pan_right; 

        out[0] = out_left;
        out[1] = out_right;
    };

    for (ma_uint64 out_i = out_start_i; out_i < out_end_i; out_i++, src_i++) {
        assert(INNER_CHANNELS == 2);
        process_frame(&source->pcmData[src_i*INNER_CHANNELS], 
                      &out[out_i*INNER_CHANNELS]);

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

/* ================= Equalizer ================== */

void Equalizer::prepareTimeData(audio_sample_t *in, size_t ch_idx) {
    // time_data: | previous block | data |
    // 2*hop_size     hop_size      hop_size
    audio_sample_t *prev_block = &previous_block_[ch_idx*hop_size_];
    std::copy_n(prev_block, hop_size_, time_data.begin());

    for (size_t idx = 0; idx < hop_size_; idx++) {
        audio_sample_t sample = in[idx * channels_ + ch_idx];

        // saving current block for next call
        prev_block[idx] = sample;

        // filling time_data for fft
        time_data[hop_size_ + idx] = sample;
    }

}

void Equalizer::applyOverlap(audio_sample_t *out, size_t ch_idx) {
    
    audio_sample_t *overlap = &overlap_add_[ch_idx*hop_size_];

    for (size_t idx = 0; idx < hop_size_; idx++) {
        // output[i] = overlapp_add[i] + time_data[i], i = 0...hop-1
        out[idx * channels_ + ch_idx] = time_data[idx] + overlap[idx];
    }

    // overlapp_add[i] = time_data[i], i = hop...2*hop-1
    std::copy_n(time_data.begin() + hop_size_, hop_size_, overlap);
}

void Equalizer::processBlock(audio_sample_t *inout) {

    for (size_t ch_idx = 0; ch_idx < channels_; ch_idx++) {

        prepareTimeData(inout, ch_idx);
 
        wfftr_.forward(time_data.data(), freq_data.data());

        const size_t spectr_size = freq_response_.size();
        for (size_t i = 0; i < spectr_size; i++) {
            freq_data[i].i *= freq_response_[i];
            freq_data[i].r *= freq_response_[i];
        }

        wfftr_.inverse(freq_data.data(), time_data.data());
        wfftr_.normalize(time_data.data());

        applyOverlap(inout, ch_idx);

    }
}

/* ================= Track    =================== */

const std::vector<audio_sample_t> &Track::renderBlock(ma_uint64 start_frame) {

    // clearing buffer and allocating memory if needed
    std::vector<audio_sample_t> &buf = rendering_buffer.writerGetBuffer(render_block_size*INNER_CHANNELS);
    
    // early out when track is muted
    if (mute) return rendering_buffer.writerSentReadyBuffer();

    // rendering clips
    for (int clip_idx = 0; clip_idx < clips.size(); clip_idx++) {
        Clip& clip = clips[clip_idx];
        clip.renderFrames(buf, start_frame, render_block_size);
    }

    // applying effects (gain + pan)
    float gain = dbToGain(gain_db);

    auto process_frame = [&](float *in_out) {
        float left = in_out[0], right = in_out[1];

        float pan_left  = (pan <= 0) ? 1 : (1 - pan);
        float pan_right = (pan >= 0) ? 1 : (1 + pan); 
        float out_left  = left  * gain * pan_left;
        float out_right = right * gain * pan_right; 

        in_out[0] = out_left;
        in_out[1] = out_right;
    };

    for (ma_uint64 idx = 0; idx < render_block_size; idx++) {
        assert(INNER_CHANNELS == 2);
        process_frame(&buf[idx*INNER_CHANNELS]);

    }

    equalizer.processBlock(buf.data());

    return rendering_buffer.writerSentReadyBuffer();
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

const std::vector<audio_sample_t>& TimeLine::renderBlock(ma_uint64 start_frame) {
    // clearing buffer and allocating memory if needed
    std::vector<audio_sample_t> &buf = rendering_buffer.writerGetBuffer(render_block_size * INNER_CHANNELS);

    float gain = dbToGain(gain_db);
    //TODO: INVALIDATE CACHE IF START_FRAME != NEXT EXPECTED FRAME
    const size_t frame_count = render_block_size;


    for (int track_idx = 0; track_idx < tracks.size(); track_idx++) {
        const auto &track_buf = getTrack(track_idx).renderBlock(start_frame);
        for (int i = 0; i < frame_count * INNER_CHANNELS; i++) {
            buf[i] += track_buf[i] * gain;
            // PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << "timeline_amp: "<< buf[i] <<
                                                            // " track_amp: " << track_buf[i];
        }   
    }
    

    return rendering_buffer.writerSentReadyBuffer();
}

void TimeLine::renderFrames(audio_sample_t *out, ma_uint64 start_frame, ma_uint64 frame_count) {

    ma_uint64 cur_frame = start_frame + block_adapter.size() / INNER_CHANNELS;
    ma_uint64 frames_left = frame_count;

    while (frames_left != 0) {
        ma_uint64 step = std::min(ma_uint64(render_block_size), frames_left);

        if (block_adapter.size() < step * INNER_CHANNELS) {
            const auto &buffer = renderBlock(cur_frame);
            cur_frame += render_block_size;

            PLOG_VERBOSE_IF(g_debug_flags.block_adapter_logs) << "Pushing " << render_block_size << "frames";
            block_adapter.push_bulk(buffer.data(), render_block_size * INNER_CHANNELS);    
            PLOG_VERBOSE_IF(g_debug_flags.block_adapter_logs) << "Size = " << block_adapter.size() / INNER_CHANNELS << "frames";
        }

        PLOG_VERBOSE_IF(g_debug_flags.block_adapter_logs) << "Popping " << step << "frames";
        block_adapter.pop_bulk(out, step * INNER_CHANNELS);
        PLOG_VERBOSE_IF(g_debug_flags.block_adapter_logs) << "Size = " << block_adapter.size() / INNER_CHANNELS << "frames";

        out += step * INNER_CHANNELS;
        frames_left -= step;
    }
}



bool TimeLine::isValidClipId(ClipId_t id) {
    return getTrackAndClipIdx(id) ? true: false;
}

std::optional<ClipLoc> TimeLine::getTrackAndClipIdx(ClipId_t id) {
    for (size_t track_idx = 0; track_idx < tracks.size(); track_idx++) {
        for (size_t clip_idx = 0; clip_idx < getTrack(track_idx).clips.size(); clip_idx++) {
            if (getTrack(track_idx).clips[clip_idx].id == id) {
                return ClipLoc{track_idx, clip_idx};
            }
        }
    }

    return std::nullopt;
}

Clip *TimeLine::getClipById(ClipId_t id) {
    auto loc = getTrackAndClipIdx(id);
    if (loc)
        return &getTrack(loc->track_idx).clips[loc->clip_idx];

    return nullptr;
}


void TimeLine::removeClipByLoc(ClipLoc loc) {
    // invalid loc
    if (tracks.size() <= loc.track_idx) {
        return;
    }

    if (getTrack(loc.track_idx).clips.size() <= loc.clip_idx) {
        return;
    }

    // Synchonized deletion
    mtx.lock();

    std::vector<Clip> &clips = getTrack(loc.track_idx).clips;
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

    std::vector<Clip>& old_clips = getTrack(track_i).clips;
    addClip(old_clips[clip_i], track_idx);

    removeClipByLoc(*loc);
}

void TimeLine::addTrack() {
    tracks.emplace_back(render_buffer_mtx);
}

ClipId_t TimeLine::addClip(const Clip& clip, int track_idx) {
    if (track_idx < 0) return CLIP_NONE;
    while (track_idx >= tracks.size())
        addTrack();

    getTrack(track_idx).addClip(clip);

    return clip.id;
}


}

