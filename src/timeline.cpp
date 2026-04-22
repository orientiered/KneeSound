#include "timeline.h"

namespace waves {

/* ================= Peak caches ================ */

PeakCache::PeakCache(ma_uint64 block_size, const std::vector<float> &samples, ma_uint64 channels) {
    block_size_ = block_size;

    ma_uint64 duration_frames = samples.size() / channels;
    size_t num_blocks = (duration_frames + block_size - 1) / block_size;
    peaks.resize(num_blocks);
    
    for (size_t b = 0; b < num_blocks; ++b) {
        ma_uint64 start = b * block_size;
        ma_uint64 end = std::min(start + block_size, duration_frames);
        for (ma_uint64 i = start; i < end; ++i) {
            float s = interleavedToMono(&samples[i * channels], channels);

            peaks[b].update(s);
        }
    }
}

PeakCache::PeakCache(ma_uint64 block_size, const PeakCache &cache) {
    block_size_ = block_size;

    assert(block_size_ % cache.block_size_ == 0);


    const size_t factor = block_size_ / cache.block_size_;
    size_t num_blocks = cache.peaks.size() / factor;

    peaks.resize(num_blocks);

    for (size_t b = 0; b < num_blocks; ++b) {
        ma_uint64 start = b * factor;
        ma_uint64 end = (b+1) * factor;

        for (ma_uint64 i = start; i < end; ++i) {
            peaks[b].update(cache.peaks[i]);
        }
    }
}

std::optional<PeakCache::min_max> PeakCacheManager::getPeak(ma_uint64 f_start, ma_uint64 f_end) const {
    const int threshold = 4;
    for (auto& cache : peak_caches) {
        if (f_end - f_start >= cache.block_size_ * threshold) { // порог: если диапазон большой
            ma_uint64 b_start = f_start / cache.block_size_;
            ma_uint64 b_end   = std::min(ma_uint64(cache.peaks.size()), (f_end + cache.block_size_ - 1) / cache.block_size_);
            PeakCache::min_max mm{0, 0};
            for (ma_uint64 b = b_start; b < b_end; ++b) {
                mm.update(cache.peaks[b]);
            }
            return mm;
        }
    }

    return std::nullopt;
}



/* ================= Clip     =================== */


std::ostream& operator<<(std::ostream& os, const Clip& clip) {
    os << "Clip '[" << &clip << "][id:" << clip.id << "] dump:\n"
       << "audio source " << clip.source << "\n"
       << "Source boundaries: [" << clip.source_start_frame << ", " << clip.source_end_frame << ")\n"
       << "Timeline start frame: " << clip.timeline_start_frame << "\n"
       << "Gain: " << clip.gain_db << " Muted: " << clip.muted << " Pan: " << clip.pan; 
    return os;
}


void Clip::getClipFrameInterpolated(audio_sample_t *out, double src_frame) const {
    ma_int64 left_src = std::min(source_end_frame-1, static_cast<ma_int64>(std::floor(src_frame)));

    audio_sample_t *left = getClipSrcFrame(left_src);
    audio_sample_t *right = getClipSrcFrame(std::min(source_end_frame-1, left_src + 1));

    float p = src_frame - left_src;

    #pragma unroll
    for (int i = 0; i < INNER_CHANNELS; i++) {
        out[i] = left[i] * (1-p) + right[i] * p;
    }
}

audio_sample_t Clip::getMonoClipFrame(ma_int64 clip_frame) const {
    float sample[INNER_CHANNELS];
    getClipFrameInterpolated(sample , clipFrameToSrcFrame(clip_frame));

    return interleavedToMono(sample, INNER_CHANNELS);
}

PeakCache::min_max Clip::getPeak(ma_int64 clip_start_frame, ma_int64 clip_end_frame) const {
    ma_int64 src_left = clipFrameToSrcFrame(clip_start_frame);
    ma_int64 src_right = clipFrameToSrcFrame(clip_end_frame);

    return source->getPeak(src_left, src_right);
}



// Renders frames to out array, ADDITIVELY 
// Doesn't write zeros
void Clip::renderFrames(std::vector<audio_sample_t> &out, ma_int64 start_frame, ma_uint64 frame_count) {

    if (muted) return;

    ma_int64 out_start_i = (start_frame >= timeline_start_frame) ? 
                            0 : timeline_start_frame - start_frame;
    
    ma_int64 out_end_i = ((start_frame + frame_count) > getTimelineEndFrame()) ? 
                            getTimelineEndFrame() - start_frame :
                            frame_count;

    if (out_start_i >= out_end_i) return;

    auto clip_i_opt = timelineToClipFrame(start_frame + out_start_i);
    if (!clip_i_opt) {
        PLOG_ERROR << "Invalid source start frame index";
        PLOG_ERROR << *this << "\n"
                   << out_start_i << " " << out_end_i << "\n";
    }
    ma_uint64 clip_i = *clip_i_opt;



    float gain = dbToGain(gain_db);
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) <<
        "Rendering clip " << id << ": si " << out_start_i << " ei " << out_end_i << " srci " << clip_i << 
        " gain " << gain;

    auto getFadeGain = [&](ma_int64 clip_frame) {
        return fade_in.getGain(0, clip_frame) * 
               fade_out.getGain(getDurationFrames(), clip_frame);
    };

    auto process_frame = [&](ma_int64 clip_frame, float *out) {
        float in[2];
        getClipFrameInterpolated(in, clipFrameToSrcFrame(clip_frame));

        float left = in[0], right = in[1];

        float pan_left  = (pan <= 0) ? 1 : (1 - pan);
        float pan_right = (pan >= 0) ? 1 : (1 + pan); 
        float fade_gain = getFadeGain(clip_frame);

        float out_left  = left  * gain * pan_left * fade_gain;
        float out_right = right * gain * pan_right * fade_gain; 

        out[0] = out_left;
        out[1] = out_right;
    };

    for (ma_uint64 out_i = out_start_i; out_i < out_end_i; out_i++, clip_i++) {
        assert(INNER_CHANNELS == 2);
        process_frame(clip_i, &out[out_i*INNER_CHANNELS]);

    }
    
}

bool Clip::stretch(bool right, ma_int64 timeline_pos) {
    
    double old_src_duration = static_cast<double>(source_end_frame - source_start_frame);

    if (!right) {
        ma_int64 new_duration = getTimelineEndFrame() - timeline_pos;
        double new_stretch = static_cast<double>(new_duration) / old_src_duration;
        
        if (new_stretch > 0) {
            double new_playback_speed = 1/new_stretch;

            bool applied = new_playback_speed == setPlaybackSpeed(new_playback_speed);
            if (applied) 
                timeline_start_frame = timeline_pos;
            return applied;
        }
    } else {
        ma_int64 new_duration = timeline_pos - timeline_start_frame;
        double new_stretch = static_cast<double>(new_duration) / old_src_duration;
        
        if (new_stretch > 0) {
            double new_playback_speed = 1/new_stretch;

            return new_playback_speed == setPlaybackSpeed(new_playback_speed);
        }
    }
    return false;
}


bool Clip::trim(bool right, ma_int64 timeline_pos) {
    ma_int64 avaialable_left = source_start_frame / playback_speed;
    ma_int64 avaialable_right = (getSourceDuration() - source_end_frame) / playback_speed;

    if (!right) {
        // trim from left
        if (timeline_pos < getTimelineEndFrame() &&
            timeline_start_frame - timeline_pos <= avaialable_left ) {
            source_start_frame += (timeline_pos - timeline_start_frame) * playback_speed;
            timeline_start_frame = timeline_pos;
            return true;
        }

    } else {
        //trim from right
        if (timeline_pos > timeline_start_frame && 
            timeline_pos - getTimelineEndFrame() <= avaialable_right) {
            source_end_frame = clipFrameToSrcFrame(timeline_pos - timeline_start_frame);
            return true;
        }
    }

    return false;
}

std::optional<Clip> Clip::cut(ma_int64 timeline_pos) {
    PLOG_DEBUG << "Cutting clip " << id << " on pos " << timeline_pos;
    std::optional<ma_uint64> clip_pos = timelineToClipFrame(timeline_pos);
    // if cut position is not in clip, do not cut
    if (!clip_pos) return std::nullopt;

    ma_uint64 source_pos = clipFrameToSrcFrame(*clip_pos);

    Clip new_clip = copy();
    new_clip.timeline_start_frame = timeline_pos;
    new_clip.source_end_frame = source_end_frame;
    new_clip.source_start_frame = source_pos;

    source_end_frame = source_pos;

    return new_clip;
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

    if (enable_eq)
        fft_pipeline.processBlock(buf.data(), equalizer);
    // if (enable_eq)
    //     fft_pipeline.processBlock(buf.data(), pitch);

    return rendering_buffer.writerSentReadyBuffer();
}

size_t Track::getLatency() {
    return fft_pipeline.getLatency() * enable_eq;
}

/* ================= Timeline =================== */

const std::vector<audio_sample_t>& TimeLine::renderBlock(ma_uint64 start_frame) {
    static ma_uint64 expected_frame = 0;

    // clearing buffer and allocating memory if needed
    std::vector<audio_sample_t> &buf = rendering_buffer.writerGetBuffer(render_block_size * INNER_CHANNELS);

    float gain = dbToGain(gain_db);
    //TODO: INVALIDATE CACHE IF START_FRAME != NEXT EXPECTED FRAME
    const size_t frame_count = render_block_size;

    

    for (int track_idx = 0; track_idx < tracks.size(); track_idx++) {
        Track &track = getTrack(track_idx);
        size_t latency = track.getLatency();

        // Pre-fill if rendering non-sequantially
        if (start_frame != expected_frame) {
            for (int pre_fill_idx = 0; pre_fill_idx < latency; pre_fill_idx++) {
                track.renderBlock(start_frame + latency * pre_fill_idx * render_block_size);
            }
        }

        const auto &track_buf = track.renderBlock(start_frame + latency * render_block_size);

        for (int i = 0; i < frame_count * INNER_CHANNELS; i++) {
            buf[i] += track_buf[i] * gain;
            // PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << "timeline_amp: "<< buf[i] <<
                                                            // " track_amp: " << track_buf[i];
        }   
    }
    
    expected_frame = start_frame + frame_count;

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

bool TimeLine::isValidTrackId(TrackId_t id) {
    return getTrackById(id);
}

Track *TimeLine::getTrackById(TrackId_t id) {
    auto it = std::find_if(tracks.begin(), tracks.end(), 
                        [&id](const Track &track) { 
                            return track.id == id;
                        });
    
    return (it == tracks.end()) ? nullptr : &*it;
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

