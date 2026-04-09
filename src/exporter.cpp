#include "wav_exporter.h"
#include <thread>
#include <atomic>

#include "imgui.h"
#include "ImGuiFileDialog.h"

#include "editor.h"

namespace waves {

/*

Source: https://en.wikipedia.org/wiki/WAV

[Master RIFF chunk]
   FileTypeBlocID  (4 bytes) : Identifier « RIFF »  (0x52, 0x49, 0x46, 0x46)
   FileSize        (4 bytes) : Overall file size minus 8 bytes
   FileFormatID    (4 bytes) : Format = « WAVE »  (0x57, 0x41, 0x56, 0x45)

[Chunk describing the data format]
   FormatBlocID    (4 bytes) : Identifier « fmt␣ »  (0x66, 0x6D, 0x74, 0x20)
   BlocSize        (4 bytes) : Chunk size minus 8 bytes, which is 16 bytes here  (0x10)
   AudioFormat     (2 bytes) : Audio format (1: PCM integer, 3: IEEE 754 float)
   NbrChannels     (2 bytes) : Number of channels
   Frequency       (4 bytes) : Sample rate (in hertz)
   BytePerSec      (4 bytes) : Number of bytes to read per second (Frequency * BytePerBloc).
   BytePerBloc     (2 bytes) : Number of bytes per block (NbrChannels * BitsPerSample / 8).
   BitsPerSample   (2 bytes) : Number of bits per sample

[Chunk containing the sampled data]
   DataBlocID      (4 bytes) : Identifier « data »  (0x64, 0x61, 0x74, 0x61)
   DataSize        (4 bytes) : SampledData size
   SampledData

Total header size = 44 bytes
SampledDataSize = byte_per_block * total_frames 
*/

/* =================== EXPORTER HELPERS ======================= */
static void writeIntNumberAsBytes(std::ostream &out, uint64_t num, int bytes_count) {
    out.write(reinterpret_cast<char*>(&num), bytes_count);
}

template<typename T>
static void writeByteSequence(std::ostream &out, const std::vector<T> &bytes) {
    const char *data = reinterpret_cast<const char*>(bytes.data());
    const size_t size = bytes.size() * sizeof(T);
    out.write(data, size);
}

void Exporter::writeWAVHeader() {

    uint64_t bits_per_sample = sizeof(audio_sample_t) * 8;
    uint64_t byte_per_block  = INNER_CHANNELS * bits_per_sample / 8;
    uint64_t byte_per_sec    = INNER_SAMPLE_RATE * byte_per_block;

    uint64_t sampled_data_size = byte_per_block * (export_end_frame - export_start_frame);
    const uint64_t header_size = 44;
    uint32_t file_size = header_size + sampled_data_size;

    // header chunk
    output_file << "RIFF";
    writeIntNumberAsBytes(output_file, file_size - 8, 4); // overall file size - 8 bytes
    output_file << "WAVE";


    // data format chunk
    writeByteSequence(output_file, std::vector<uint8_t>{0x66, 0x6D, 0x74, 0x20}); // "fmt "
    writeIntNumberAsBytes(output_file, 16, 4);
    writeIntNumberAsBytes(output_file, 3, 2); // IEEE 754 float
    writeIntNumberAsBytes(output_file, INNER_CHANNELS, 2); // channels
    writeIntNumberAsBytes(output_file, INNER_SAMPLE_RATE, 4); // sample rate

    writeIntNumberAsBytes(output_file, byte_per_sec, 4);
    writeIntNumberAsBytes(output_file, byte_per_block, 2);
    writeIntNumberAsBytes(output_file, bits_per_sample, 2);

    // sampled data chunk beginning
    output_file << "data"; // identifier
    writeIntNumberAsBytes(output_file, sampled_data_size, 4);

}

void Exporter::encodeAudio(encoder_callback_t callback, void *data) {
    uint64_t current_frame = export_start_frame;
    uint64_t step = preferred_render_step;

    while (current_frame < export_end_frame) {
        // updating status
        current_export_frame_ = current_frame; 

        // writing frames
        uint64_t frame_count = ((current_frame + step) >= export_end_frame ) ? 
                                export_end_frame - current_frame :
                                step;

        std::vector<audio_sample_t> &frames = callback(data, current_frame, frame_count);
        current_frame += frame_count;
        writeByteSequence(output_file, frames);

    }

    output_file.close();
    PLOG_INFO << "Encoding finished!";
    ready.store(true);
}

/* ================== ENCODE START ============================= */
bool Exporter::startEncoding(encoder_callback_t callback, void *data) {
    if (!output_file.good()) {
        PLOG_ERROR << "Encoder: output file is not properly opened ";
        return false;
    }

    if (export_end_frame < export_start_frame) {
        PLOG_ERROR << "Encoder: start and end frames are invalid";
        return false;
    }

    // will only launch if encoder is not processing anything elses
    bool ready_ref = true;
    bool encoder_is_ready = ready.compare_exchange_strong(ready_ref, false);
    if (!encoder_is_ready) {
        PLOG_ERROR << "Encoder is busy";
        return false;
    }

    PLOG_INFO << "Starting encoding to file " << output_path;

    current_export_frame_ = export_start_frame;

    PLOG_DEBUG << "Writing WAV Header";
    writeWAVHeader();
    
    PLOG_DEBUG << "Launching encoder thread";
    std::thread encoding_thread(&Exporter::encodeAudio, this, callback, data);

    encoding_thread.detach();

    PLOG_DEBUG << "Encoding started";

    return true;
}

/* ================== EXPORTER INTERFACE ================ */
bool Exporter::setOutputPath(const std::string &path) {
    // processing only if encoder is ready
    if (!ready.load()) return false;

    PLOG_DEBUG << "Setting path " << path;

    output_file.open(path, std::ios::out | std::ios::trunc);

    return output_file.good();
}

int32_t Exporter::setStartFrame(int32_t frame) {
    // processing only if encoder is ready
    if (!ready.load()) return export_start_frame;

    export_start_frame = std::max(0, frame);
    export_end_frame = std::max(export_start_frame, export_end_frame); 
    
    return export_start_frame;
}

int32_t Exporter::setEndFrame(int32_t frame) {
    // processing only if encoder is ready
    if (!ready.load()) return export_end_frame;
    export_end_frame = std::max(0, frame);

    export_start_frame = std::min(export_start_frame, export_end_frame);

    return export_end_frame;
}


/* ========================== EXPORT CALLBACK =========================== */

static std::vector<audio_sample_t> &timeline_render_callback(void *data, uint64_t start_frame, uint64_t frame_count) {
    TimeLine *timeline = reinterpret_cast<TimeLine *>(data);

    return timeline->renderFrames(start_frame, frame_count);
}

/* ========================== EXPORTER VIEW IN EDITOR =================== */


void Exporter_View::Draw(Editor& editor) {
    
    static std::string output_path = "";

    const char *label = (output_path == "") ? "Choose file" : output_path.c_str();
    if (ImGui::Button(label)) {
        IGFD::FileDialogConfig config;
        config.path = "."; // starting from current directory;
        config.countSelectionMax = 1; // selecting 1 file
        ImGuiFileDialog::Instance()->OpenDialog("ChooseExportPathKey", "Save file", ".wav", config);
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseExportPathKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK

            output_path = ImGuiFileDialog::Instance()->GetFilePathName();
            if (!exporter.setOutputPath(output_path)) {
                output_path = "Invalid path, choose again";
            }
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }

    ImGui::Separator();
    ImGui::Text("Export range");
    static std::pair<int32_t, int32_t> export_range;

    export_range = exporter.getExportRange();

    int32_t max_frame = editor.tl_view.getTimelineLen();

    if (ImGui::SliderInt2("##export_range_slider", reinterpret_cast<int32_t*>(&export_range), 
                    0, max_frame, "%u")) {
        exporter.setStartFrame(export_range.first);
        exporter.setEndFrame(export_range.second);
    }  

    if (ImGui::Button("Set start to playhead")) {
        exporter.setStartFrame(editor.timeline.playhead_frame);   
    }
    ImGui::SameLine();

    if (ImGui::Button("Set end to playhead")) {
        exporter.setEndFrame(editor.timeline.playhead_frame);   
    }

    export_range = exporter.getExportRange();

    float length_in_sec = static_cast<float>(export_range.second - export_range.first) / INNER_SAMPLE_RATE;
    ImGui::Text("Estimated length: %.3f sec", length_in_sec);

    static bool encoder_started = false;
    static bool error_on_start = false;
    static bool encoder_finished = false;

    bool new_started = !exporter.getReadyState();
    if (encoder_started && !new_started) {
        encoder_finished = true;
        output_path = ""; // resetting path
    } 
    encoder_started = new_started;

    if (encoder_started ) {
        float progress_percent =
            static_cast<float>(exporter.getEncodingProgress()) / (export_range.second - export_range.first);
        ImGui::ProgressBar(progress_percent);
    } else {
        if (encoder_finished) {
            ImGui::Text("Success!");
        }

        if (error_on_start) {
            ImGui::Text("Failed to start encoding");
        }

        if (ImGui::Button("Export")) {  
            encoder_finished = false;
            error_on_start = !exporter.startEncoding(timeline_render_callback, &editor.timeline);
        }

    }
}

}