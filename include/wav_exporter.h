#pragma once

#include <fstream>
#include "common.h"
#include "playback_state.h"

namespace waves {

class Editor;

using encoder_callback_t = 
    std::vector<audio_sample_t> &(void *data, uint64_t start_frame, uint64_t frame_count);

class Exporter {

    std::string output_path;
    std::fstream output_file;
    int32_t export_start_frame = 0, export_end_frame = 0;

    uint32_t preferred_render_step = 8192; 
    // progress status
    std::atomic<bool> ready = true;
    int32_t current_export_frame_ = 0;
public: 
    std::pair<int32_t, int32_t> getExportRange() { 
        return {export_start_frame, export_end_frame};
    }

    // These functions will change state only when exporter is ready
    bool setOutputPath(const std::string &path);

    // set start frame and return its new value
    int32_t setStartFrame(int32_t frame);

    // set end   frame and return its new value
    int32_t setEndFrame(int32_t frame);

    // Queue current exporting frame 
    int32_t getEncodingProgress() { return current_export_frame_ - export_start_frame; }
    int32_t getReadyState() { return ready.load(); }

    // start Encoding in separate thread
    bool startEncoding(encoder_callback_t callback, void *callback_data);
private:
    // Helper functions
    void writeWAVHeader();
    void encodeAudio(encoder_callback_t callback, void *callback_data);

};

class Exporter_View {
    Exporter exporter;
public:
    void Draw(Editor& editor);
};

}