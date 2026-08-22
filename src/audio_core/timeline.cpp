#include "common.h"
#include "core/timeline.h"
#include "core/miniaudio_utils.h"
#include "effects/audio_effects.h"
#include "serialization.h"
#include "utils/buffer_utils.h"
#include "core/audio_source.h"

#include <cstdint>
#include <filesystem>
namespace waves {

/* ================= Peak caches ================ */

PeakCache::PeakCache(uint64_t block_size, const AudioBuffer &samples) {
    block_size_ = block_size;

    uint64_t duration_frames = samples.getFrameCount();
    size_t num_blocks = (duration_frames + block_size - 1) / block_size;
    peaks.resize(num_blocks);

    for (size_t b = 0; b < num_blocks; ++b) {
        uint64_t start = b * block_size;
        uint64_t end = std::min(start + block_size, duration_frames);
        for (uint64_t i = start; i < end; ++i) {
            peaks[b].update(samples.getMeanSample(i));
        }
    }
}

PeakCache::PeakCache(uint64_t block_size, const PeakCache &cache) {
    block_size_ = block_size;

    assert(block_size_ % cache.block_size_ == 0);


    const size_t factor = block_size_ / cache.block_size_;
    size_t num_blocks = cache.peaks.size() / factor;

    peaks.resize(num_blocks);

    for (size_t b = 0; b < num_blocks; ++b) {
        uint64_t start = b * factor;
        uint64_t end = (b+1) * factor;

        for (uint64_t i = start; i < end; ++i) {
            peaks[b].update(cache.peaks[i]);
        }
    }
}

std::optional<PeakCache::min_max> PeakCacheManager::getPeak(uint64_t f_start, uint64_t f_end) const {
    const int threshold = 4;
    for (auto& cache : peak_caches) {
        if (f_end - f_start >= cache.block_size_ * threshold) { // порог: если диапазон большой
            uint64_t b_start = f_start / cache.block_size_;
            uint64_t b_end   = std::min(uint64_t(cache.peaks.size()), (f_end + cache.block_size_ - 1) / cache.block_size_);
            PeakCache::min_max mm{0, 0};
            for (uint64_t b = b_start; b < b_end; ++b) {
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

audio_sample_t Clip::getClipFrameInterpolated(uint32_t channel, double src_frame) const {
    int64_t left_src = std::min(source_end_frame-1, static_cast<int64_t>(std::floor(src_frame)));

    audio_sample_t left = getClipSrcFrame(channel, left_src);
    audio_sample_t right = getClipSrcFrame(channel, std::min(source_end_frame-1, left_src + 1));

    float p = src_frame - left_src;

    return left * (1 - p) + right * p;
}

audio_sample_t Clip::getMonoClipFrame(int64_t clip_frame) const {
    double src_frame = clipFrameToSrcFrame(clip_frame);
    return (getClipFrameInterpolated(0, src_frame) + getClipFrameInterpolated(1, src_frame)) * 0.5;
}

PeakCache::min_max Clip::getPeak(int64_t clip_start_frame, int64_t clip_end_frame) const {
    int64_t src_left = clipFrameToSrcFrame(clip_start_frame);
    int64_t src_right = clipFrameToSrcFrame(clip_end_frame);

    return source->getPeak(src_left, src_right);
}



// Renders frames to out array, ADDITIVELY
// Doesn't write zeros
void Clip::renderFrames(AudioBuffer &out, int64_t start_frame, uint64_t frame_count) {

    if (!source || !source->valid) return;
    if (muted) return;

    int64_t out_start_i = (start_frame >= timeline_start_frame) ?
                            0 : timeline_start_frame - start_frame;

    int64_t out_end_i = ((start_frame + frame_count) > getTimelineEndFrame()) ?
                            getTimelineEndFrame() - start_frame :
                            frame_count;

    if (out_start_i >= out_end_i) return;

    auto clip_i_opt = timelineToClipFrame(start_frame + out_start_i);
    if (!clip_i_opt) {
        PLOG_ERROR << "Invalid source start frame index";
        PLOG_ERROR << *this << "\n"
                   << out_start_i << " " << out_end_i << "\n";
    }
    uint64_t start_clip_frame = *clip_i_opt;

    float gain = dbToGain(gain_db);
    PLOG_VERBOSE_IF(g_debug_flags.callback_logs) <<
        "Rendering clip " << id << ": si " << out_start_i << " ei " << out_end_i << " srci " << start_clip_frame <<
        " gain " << gain;

    auto getFadeGain = [&](int64_t clip_frame) {
        return fade_in.getGain(0, clip_frame) *
               fade_out.getGain(getDurationFrames(), clip_frame);
    };

    float pan_left  = (pan <= 0) ? 1 : (1 - pan);
    float pan_right = (pan >= 0) ? 1 : (1 + pan);

    auto process_frame = [&](size_t ch, int64_t clip_frame) -> audio_sample_t {

        float s = getClipFrameInterpolated(ch, clipFrameToSrcFrame(clip_frame));

        float fade_gain = getFadeGain(clip_frame);
        float pan = (ch == 0) ? pan_left : pan_right;

        return s * fade_gain * gain * pan;
    };

    assert(INNER_CHANNELS == 2);
    for (size_t ch = 0; ch < INNER_CHANNELS; ch++) {
        audio_sample_t *ch_out = out.getChannel(ch);
        for (uint64_t out_i = out_start_i, clip_frame = start_clip_frame;
                       out_i < out_end_i;
                       out_i++, clip_frame++) {
            ch_out[out_i] += process_frame(ch, clip_frame);
        }
    }

}

bool Clip::stretch(bool right, int64_t timeline_pos) {

    double old_src_duration = static_cast<double>(source_end_frame - source_start_frame);

    if (!right) {
        int64_t new_duration = getTimelineEndFrame() - timeline_pos;
        double new_stretch = static_cast<double>(new_duration) / old_src_duration;

        if (new_stretch > 0) {
            double new_playback_speed = 1/new_stretch;

            bool applied = new_playback_speed == setPlaybackSpeed(new_playback_speed);
            if (applied)
                timeline_start_frame = timeline_pos;
            return applied;
        }
    } else {
        int64_t new_duration = timeline_pos - timeline_start_frame;
        double new_stretch = static_cast<double>(new_duration) / old_src_duration;

        if (new_stretch > 0) {
            double new_playback_speed = 1/new_stretch;

            return new_playback_speed == setPlaybackSpeed(new_playback_speed);
        }
    }
    return false;
}


bool Clip::trim(bool right, int64_t timeline_pos) {
    int64_t avaialable_left = source_start_frame / playback_speed;
    int64_t avaialable_right = (getSourceDuration() - source_end_frame) / playback_speed;

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

std::optional<Clip> Clip::cut(int64_t timeline_pos) {
    PLOG_DEBUG << "Cutting clip " << id << " on pos " << timeline_pos;
    std::optional<uint64_t> clip_pos = timelineToClipFrame(timeline_pos);
    // if cut position is not in clip, do not cut
    if (!clip_pos) return std::nullopt;

    uint64_t source_pos = clipFrameToSrcFrame(*clip_pos);

    Clip new_clip = copy();
    new_clip.timeline_start_frame = timeline_pos;
    new_clip.source_end_frame = source_end_frame;
    new_clip.source_start_frame = source_pos;

    source_end_frame = source_pos;

    return new_clip;
}

Clip::Clip(ProjectReader input) {
    std::optional<ClipId_t> cid = input.read<ClipId_t>("id");
    if (!cid) {
        setUniqueId();
    } else {
        id = *cid;
        unique_id_ = std::max(unique_id_, id+1);
    }

    if (auto src_reader = input.nest("source")) {
        if (auto src_path_opt = src_reader->read<std::string>("file") ) {
            std::filesystem::path path(*src_path_opt);
            auto &media_map = input.getCtx().media_map;
            if (media_map.contains(path)) {
                source = media_map[path];
            } else {
                source = decode_audio_from_file(path.filename(), path);
                media_map[*src_path_opt] = source; 
            }
        }
    } 

    DESERIALIZE_SIMPLE(input, timeline_start_frame, 0);
    DESERIALIZE_SIMPLE(input, source_start_frame, 0);
    DESERIALIZE_SIMPLE(input, source_end_frame, 0);

    DESERIALIZE_OPT(input, playback_speed);
    DESERIALIZE_OPT(input, gain_db);
    DESERIALIZE_OPT(input, pan);
    DESERIALIZE_OPT(input, muted);
    
    fade_in.duration = input.read<int64_t>("fade_in", 0);
    fade_out.duration = input.read<int64_t>("fade_out", 0);
}

void Clip::serialize(ProjectWriter output) const {
    SERIALIZE_SIMPLE(output, id);
    output.write("id", id);
    output.nest("source").write("file", source->path);

    SERIALIZE_SIMPLE(output, source_start_frame);
    SERIALIZE_SIMPLE(output, source_end_frame);
    SERIALIZE_SIMPLE(output, timeline_start_frame);

    SERIALIZE_SIMPLE(output, playback_speed);
    SERIALIZE_SIMPLE(output, gain_db);
    SERIALIZE_SIMPLE(output, pan);
    SERIALIZE_SIMPLE(output, muted);

    output.write("fade_in", fade_in.duration);
    output.write("fade_out", fade_out.duration);
}


/* ================= Track    =================== */

const AudioBuffer& Track::renderBlock(uint64_t start_frame) {

    // clearing buffer
    rendering_buffer.clear();

    // early out when track is muted
    if (mute) return rendering_buffer;

    // rendering clips
    for (int clip_idx = 0; clip_idx < clips.size(); clip_idx++) {
        Clip& clip = clips[clip_idx];
        clip.renderFrames(rendering_buffer, start_frame, render_block_size);
    }

    // applying effects (gain + pan)
    float gain = dbToGain(gain_db);

    float pan_left  = (pan <= 0) ? 1 : (1 - pan);
    float pan_right = (pan >= 0) ? 1 : (1 + pan);

    auto process_frame = [&](size_t ch, audio_sample_t sample) -> audio_sample_t {
        float pan = (ch == 0) ? pan_left : pan_right;

        return sample * gain * pan;
    };

    for (size_t ch = 0; ch < INNER_CHANNELS; ch++) {
        audio_sample_t *ch_out = rendering_buffer.getChannel(ch);
        for (uint64_t idx = 0; idx < render_block_size; idx++) {
            ch_out[idx] = process_frame(ch, ch_out[idx]);
        }
    }

    effects_.processBlock(rendering_buffer);

    return rendering_buffer;
}

size_t Track::getLatency() {
    return effects_.getLatency();
}

void Track::serialize(ProjectWriter output) const {
    SERIALIZE_SIMPLE(output, id);
    SERIALIZE_SIMPLE(output, gain_db);
    SERIALIZE_SIMPLE(output, pan);
    SERIALIZE_SIMPLE(output, mute);
    output.array("clips");
    for (const Clip& clip: clips) {
        clip.serialize(output.push_back("clips"));
    }

    effects_.serialize(output.nest("effects"));
}

void Track::deserialize(ProjectReader input) {
    TrackId_t tid = input.read("id", TRACK_NONE);
    if (tid == TRACK_NONE) {
        setUniqueId();
    } else {
        id = tid;
        unique_id_ = std::max(unique_id_, tid+1);
    }

    DESERIALIZE_OPT(input, gain_db);
    DESERIALIZE_OPT(input, pan);
    DESERIALIZE_OPT(input, mute);

    std::size_t clip_cnt = input.arr_size("clips");
    PLOG_DEBUG << "\tDeserializing " << clip_cnt << " clips";
    // TODO: stop playing or make atomic replacement 
    clips.clear();
    for (std::size_t idx = 0; idx < clip_cnt; idx++) {
        PLOG_DEBUG << "\tDeserialising clip " << idx;
        ProjectReader arr_reader = input.read_array("clips", idx);
        addClip(Clip(arr_reader));
    }

    if (auto effects_reader = input.nest("effects")) {
        effects_.deserialize(*effects_reader);
    }
}


/* ================= Timeline =================== */

const AudioBuffer& TimeLine::renderBlock(uint64_t start_frame) {
    static uint64_t expected_frame = 0;

    // clearing buffer
    AudioBuffer &buf = rendering_buffer;
    buf.clear();

    //TODO: INVALIDATE CACHE IF START_FRAME != NEXT EXPECTED FRAME
    const size_t frame_count = render_block_size;

    // rendering tracks
    for (int track_idx = 0; track_idx < tracks.size(); track_idx++) {
        Track &track = getTrack(track_idx);
        size_t latency = track.getLatency();

        // Pre-fill if rendering non-sequentially
        if (start_frame != expected_frame) {
            for (int pre_fill_idx = 0; pre_fill_idx * render_block_size < latency; pre_fill_idx++) {
                track.renderBlock(start_frame + latency * pre_fill_idx);
            }
        }

        const auto &track_buf = track.renderBlock(start_frame + latency);

        for (int ch = 0; ch < INNER_CHANNELS; ch++) {
            audio_sample_t *ch_out = buf.getChannel(ch);
            const audio_sample_t *ch_track = track_buf[ch];
            for (int i = 0; i < frame_count; i++) {
                ch_out[i] += ch_track[i];
            // PLOG_VERBOSE_IF(g_debug_flags.callback_logs) << "timeline_amp: "<< buf[i] <<
                                                            // " track_amp: " << track_buf[i];
            }
        }
    }

    // applying effects (gain + pan)
    float gain = dbToGain(gain_db);

    float pan_left  = (pan <= 0) ? 1 : (1 - pan);
    float pan_right = (pan >= 0) ? 1 : (1 + pan);

    auto process_frame = [&](size_t ch, audio_sample_t sample) -> audio_sample_t {
        float pan = (ch == 0) ? pan_left : pan_right;

        return sample * gain * pan;
    };

    for (size_t ch = 0; ch < INNER_CHANNELS; ch++) {
        audio_sample_t *ch_out = buf.getChannel(ch);
        for (uint64_t idx = 0; idx < render_block_size; idx++) {
            ch_out[idx] = process_frame(ch, ch_out[idx]);
        }
    }

    effects_.processBlock(buf);

    expected_frame = start_frame + frame_count;

    return buf;
}

const std::vector<audio_sample_t> &TimeLine::renderBlockInterleaved(uint64_t start_frame) {
    const AudioBuffer &buffer = renderBlock(start_frame);
    interleave_f32_frames(INNER_CHANNELS, render_block_size, buffer.data(), interleave_buffer.data());
    return  interleave_buffer;
}


void TimeLine::renderFrames(audio_sample_t *out, uint64_t start_frame, uint64_t frame_count) {

    uint64_t cur_frame = start_frame + block_adapter.size() / INNER_CHANNELS;
    uint64_t frames_left = frame_count;

    while (frames_left != 0) {
        uint64_t step = std::min(uint64_t(render_block_size), frames_left);

        if (block_adapter.size() < step * INNER_CHANNELS) {
            const auto &buffer = renderBlockInterleaved(cur_frame);
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

    // Synchronized deletion
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
    tracks.emplace_back();
}

ClipId_t TimeLine::addClip(const Clip& clip, int track_idx) {
    if (track_idx < 0) return CLIP_NONE;
    while (track_idx >= tracks.size())
        addTrack();

    getTrack(track_idx).addClip(clip);

    return clip.id;
}

void TimeLine::serialize(ProjectWriter output) const {
    SERIALIZE_SIMPLE(output, playhead_frame);
    SERIALIZE_SIMPLE(output, gain_db);
    SERIALIZE_SIMPLE(output, pan);  
    output.array("tracks");
    for (const Track& track: tracks) {
        track.serialize(output.push_back("tracks"));
    }

    effects_.serialize(output.nest("effects"));
} 

void TimeLine::deserialize(ProjectReader input) {
    // uint64_t playhead_f = 
    playhead_frame = input.read<uint64_t>("playhead_frame", 0);

    DESERIALIZE_OPT(input, gain_db);
    DESERIALIZE_OPT(input, pan);

    std::size_t track_cnt = input.arr_size("tracks");
    PLOG_DEBUG << "Deserializing " << track_cnt << " tracks";
    // TODO: stop playing or make atomic replacement 
    tracks.clear();
    for (std::size_t idx = 0; idx < track_cnt; idx++) {
        addTrack();
        ProjectReader arr_reader = input.read_array("tracks", idx);
        PLOG_DEBUG << "Deserializing track " << idx;
        tracks.back().deserialize(arr_reader);
    }

    if (auto effects_reader = input.nest("effects")) {
        effects_.deserialize(*effects_reader);
    }
}


}
