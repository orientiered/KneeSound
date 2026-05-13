#pragma once

#include <atomic>
#include <fstream>
#include "common.h"

namespace waves {

class Editor;

using encoder_callback_t =
   void (*)(void *data, audio_sample_t *out, uint64_t start_frame, uint64_t frame_count);

using encode_finish_callback_t =
   void (*) (void *data);

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
    bool startEncoding(encoder_callback_t callback, void *callback_data,
        encode_finish_callback_t finish = nullptr, void *finish_data = nullptr);
private:
    // Helper functions
    void writeWAVHeader();
    void encodeAudio(encoder_callback_t callback, void *callback_data,
        encode_finish_callback_t finish = nullptr, void *finish_data = nullptr);

};

class Exporter_View {
public:
    void Draw(Editor& editor);
private:
    Exporter exporter;

    std::string output_path_ = "";
    std::pair<int32_t, int32_t> export_range_;

    // helpers
    void handlePathChoose();
    void handleRangeChoose(uint64_t frame);
    void handleExport(Editor &editor);
};

}
