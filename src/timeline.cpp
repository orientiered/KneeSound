#include "timeline.h"

namespace waves {

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



}

