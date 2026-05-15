#include <atomic>
#include "wav_exporter.h"
#include <thread>
#include <utility>

#include "common.h"
#include "core/playback_controller.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include "imgui_misc.h"

#include "editor.h"
#include "plog/Log.h"

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

void Exporter::encodeAudio(encoder_callback_t callback, void *data,
    encode_finish_callback_t finish, void *finish_data)
{
    uint64_t current_frame = export_start_frame;
    uint64_t step = preferred_render_step;

    std::vector<audio_sample_t> frames(step*INNER_CHANNELS);

    while (current_frame < export_end_frame) {
        // updating status
        current_export_frame_ = current_frame;

        // writing frames
        uint64_t frame_count = ((current_frame + step) >= export_end_frame ) ?
                                export_end_frame - current_frame :
                                step;

        frames.resize(frame_count * INNER_CHANNELS);
        callback(data, frames.data(), current_frame, frame_count);

        current_frame += frame_count;
        writeByteSequence(output_file, frames);

    }

    output_file.close();
    PLOG_DEBUG<< "Calling finish callback";
    if (finish)
            finish(finish_data);

    PLOG_INFO << "Encoding finished!";

    ready.store(true);
}

/* ================== ENCODE START ============================= */
bool Exporter::startEncoding(encoder_callback_t callback, void *data,
    encode_finish_callback_t finish, void *finish_data)
{
    if (!output_file.is_open()) {
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
    std::thread encoding_thread(&Exporter::encodeAudio, this, callback, data, finish, finish_data);

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
    output_path = path;

    return output_file.is_open();
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

void timeline_render_callback(void *data, audio_sample_t *out, uint64_t start_frame, uint64_t frame_count) {
    TimeLine *timeline = reinterpret_cast<TimeLine *>(data);

    timeline->renderFrames(out, start_frame, frame_count);
}

void playback_finish_callback(void *data) {
    PlaybackController *controller = reinterpret_cast<PlaybackController *>(data);

    controller->player.start();
}
/* ========================== EXPORTER VIEW IN EDITOR =================== */

void Exporter_View::handlePathChoose() {
    const char *label = (output_path_ == "") ? "Choose file" : output_path_.c_str();
    const char * const FILE_CHOOSE_KEY = "ChooseExportPathKey";
    if (ImGui::Button(label)) {
        IGFD::FileDialogConfig config;
        config.path = "."; // starting from current directory;
        config.countSelectionMax = 1; // selecting 1 file
        ImGuiFileDialog::Instance()->OpenDialog(FILE_CHOOSE_KEY, "Save file", ".wav", config);
    }

    if (ImGuiFileDialog::Instance()->Display(FILE_CHOOSE_KEY)) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK

            output_path_ = ImGuiFileDialog::Instance()->GetFilePathName();
            if (!exporter.setOutputPath(output_path_)) {
                output_path_ = "Invalid path, choose again";
            }
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }
}

void Exporter_View::handleRangeChoose(uint64_t frame) {
    ImGui::Text("Export range");

    export_range_ = exporter.getExportRange();

    int32_t max_frame = INNER_SAMPLE_RATE * 60 * 60; // 1 hour limit for now
    // int32_t max_frame = editor.tl_view.getTimelineLen();

    if (ImGui::SliderInt2("##export_range_slider", reinterpret_cast<int32_t*>(&export_range_),
                    0, max_frame, "%u")) {
        exporter.setStartFrame(export_range_.first);
        exporter.setEndFrame(export_range_.second);
    }

    if (ImGui::Button("Set start to playhead")) {
        exporter.setStartFrame(frame);
    }
    ImGui::SameLine();

    if (ImGui::Button("Set end to playhead")) {
        exporter.setEndFrame(frame);
    }

    export_range_ = exporter.getExportRange();

    float length_in_sec = static_cast<float>(export_range_.second - export_range_.first) / INNER_SAMPLE_RATE;
    ImGui::Text("Estimated length: %.3f sec", length_in_sec);
}

void Exporter_View::handleExport(Editor &editor) {
    static bool encoder_started = false;
    static bool error_on_start = false;
    static bool encoder_finished = false;

    bool new_started = !exporter.getReadyState();
    if (encoder_started && !new_started) {
        encoder_finished = true;
        output_path_ = ""; // resetting path
    }
    encoder_started = new_started;

    if (encoder_started ) {
        float progress_percent =
            static_cast<float>(exporter.getEncodingProgress()) / (export_range_.second - export_range_.first);
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

            editor.playback_state.player.stop();
            error_on_start = !exporter.startEncoding(timeline_render_callback, &editor.timeline, playback_finish_callback, &editor.playback_state);
            if (!error_on_start) encoder_started = true;
            else {
                editor.playback_state.player.start();
            }
        }

    }
}

// Calculate RMS for 2 channel interleaved float audio
static std::pair<float, float> calculateRMS2(float *data, uint32_t frame_count) {
    float left_sum = 0, right_sum = 0;

    for (uint32_t frame = 0; frame < frame_count; frame++) {
        float left = data[frame * 2];
        float right = data[frame * 2 + 1];

        left_sum += left * left;
        right_sum += right * right;
    }

    float left_rms = std::sqrt(left_sum / frame_count);
    float right_rms = std::sqrt(right_sum / frame_count);

    float left_log_rms = 20.0f * std::log10(std::max(left_rms, 1e-10f));
    float right_log_rms = 20.0f * std::log10(std::max(right_rms, 1e-10f));

    // return {left_log_rms, right_log_rms};
    return {left_rms, right_rms};
}

// Calculate max for 2 channel interleaved float audio
static std::pair<float, float> calculateMax2(float *data, uint32_t frame_count) {
    float left_max = 0, right_max = 0;

    for (uint32_t frame = 0; frame < frame_count; frame++) {
        float left = data[frame * 2];
        float right = data[frame * 2 + 1];

        left_max = std::max(std::abs(left), left_max);
        right_max = std::max(std::abs(right), right_max);
    }

    return {left_max, right_max};
}

void PlotAudioBlockStats(const std::vector<BlockStats>& data, float sample_offset, float clip_threshold = 1.0f,
        float sample_rate = INNER_SAMPLE_RATE,
        int block_size = 8192)
{
    if (data.size() < 2) {
        ImGui::Text("Требуется минимум 2 блока для отрисовки.");
        return;
    }

    // Область графика внутри ImGui
    ImGui::BeginChild("##AudioPlot", ImVec2(0, 300.0f), true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    const float margin = 50.0f;
    const float plot_w = avail.x - margin;
    const float plot_h = avail.y - margin;

    // Фиксированный диапазон Y для аудио (-1.2 .. +1.2).
    // Можно заменить на авто-масштабирование по min/max данных.
    const float y_min = -1.2f, y_max = 1.2f;
    const float y_range = y_max - y_min;

    // Функции преобразования данных в экранные координаты
    auto to_x = [&](size_t i) { return p_min.x + (i / (float)(data.size() - 1)) * plot_w; };
    auto to_y = [&](float v) { return p_min.y + plot_h - ((v - y_min) / y_range) * plot_h; };

    // 1. Фон графика
    dl->AddRectFilled(p_min, ImVec2(p_min.x + plot_w, p_min.y + plot_h), IM_COL32(28, 28, 28, 255));

    // 2. Линии порога клиппинга (+1.0 и -1.0)
    float th_y_pos = to_y(clip_threshold);
    float th_y_neg = to_y(-clip_threshold);
    dl->AddLine(ImVec2(p_min.x, th_y_pos), ImVec2(p_min.x + plot_w, th_y_pos), IM_COL32(255, 120, 120, 140), 1.5f);
    dl->AddLine(ImVec2(p_min.x, th_y_neg), ImVec2(p_min.x + plot_w, th_y_neg), IM_COL32(255, 120, 120, 140), 1.5f);

    // 3. Отрисовка графиков RMS и Max
    ImVec2 prev_rms, prev_max;
    for (size_t i = 0; i < data.size(); ++i) {
        ImVec2 curr;
        // Клиппинг определяется по пиковой амплитуде блока
        bool is_clipped = std::abs(data[i].max_amp) >= clip_threshold;

        // RMS линия
        curr.x = to_x(i);
        curr.y = to_y(data[i].rms);
        if (i > 0) {
            ImU32 col = is_clipped ? IM_COL32(255, 80, 80, 220) : IM_COL32(100, 180, 255, 180);
            dl->AddLine(prev_rms, curr, col, 1.5f);
        }
        prev_rms = curr;

        // Max линия
        curr.y = to_y(data[i].max_amp);
        if (i > 0) {
            ImU32 col = is_clipped ? IM_COL32(255, 0, 0, 255) : IM_COL32(120, 255, 120, 200);
            dl->AddLine(prev_max, curr, col, 2.0f);
        }
        prev_max = curr;
    }

    /* ========== TOOLTIP ===============  */
    ImVec2 mouse_pos = ImGui::GetMousePos();
        bool in_plot = (mouse_pos.x >= p_min.x && mouse_pos.x <= p_min.x + plot_w &&
                        mouse_pos.y >= p_min.y && mouse_pos.y <= p_min.y + plot_h &&
                        ImGui::IsWindowHovered());

    if (in_plot) {
        // Привязка к ближайшему блоку
        float rel_x = mouse_pos.x - p_min.x;
        size_t idx = std::clamp<size_t>(std::round(rel_x / plot_w * (data.size() - 1)), 0, data.size() - 1);
        float snap_x = to_x((float)idx);

        // Вертикальный курсор
        dl->AddLine(ImVec2(snap_x, p_min.y), ImVec2(snap_x, p_min.y + plot_h), IM_COL32(255, 255, 255, 150), 1.0f);

        // Расчёт времени
        double total_samples = (double)idx * block_size + sample_offset;
        double time_sec = total_samples / sample_rate;

        int total_ms = (int)std::round((time_sec - std::floor(time_sec)) * 1000);
        int total_s  = (int)std::floor(time_sec);
        int s = total_s % 60;
        int m = (total_s / 60) % 60;
        int h = total_s / 3600;

        char time_buf[20];
        std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d.%03d", h, m, s, total_ms);

        const auto& blk = data[idx];
        bool is_clipped = std::abs(blk.max_amp) >= clip_threshold;

        ImGui::BeginTooltip();
        ImGui::Text("Block: %zu / %zu", idx, data.size() - 1);
        ImGui::Text("Time:  %s", time_buf);
        ImGui::Separator();
        ImGui::Text("RMS:   %.4f", blk.rms);
        ImGui::Text("Max:   %.4f", blk.max_amp);
        if (is_clipped)
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "⚠ CLIPPED");
        else
            ImGui::Text("Status: OK");
        ImGui::EndTooltip();
    }

    /* ========== ПОДПИСИ ===============  */


    // 4. Подписи осей
    char buf[32];
    std::snprintf(buf, sizeof(buf), "+%.1f", y_max);
    dl->AddText(ImVec2(p_min.x , p_min.y - 6), IM_COL32(200, 200, 200, 255), buf);
    std::snprintf(buf, sizeof(buf), "%.1f", y_min);
    dl->AddText(ImVec2(p_min.x, p_min.y + plot_h - 6), IM_COL32(200, 200, 200, 255), buf);
    dl->AddText(ImVec2(p_min.x, p_min.y + plot_h / 2.0f - 6), IM_COL32(200, 200, 200, 255), "0.0");

    // 5. Легенда внутри области графика
    float lg_x = p_min.x + plot_w - 110;
    float lg_y = p_min.y + 15;
    dl->AddText(ImVec2(lg_x, lg_y), IM_COL32_WHITE, "RMS");
    dl->AddLine(ImVec2(lg_x - 18, lg_y + 4), ImVec2(lg_x - 5, lg_y + 4), IM_COL32(100, 180, 255, 180), 2.0f);

    dl->AddText(ImVec2(lg_x, lg_y + 20), IM_COL32_WHITE, "Max");
    dl->AddLine(ImVec2(lg_x - 18, lg_y + 24), ImVec2(lg_x - 5, lg_y + 24), IM_COL32(120, 255, 120, 200), 2.0f);

    dl->AddText(ImVec2(lg_x, lg_y + 40), IM_COL32_WHITE, "Clip > 1.0");
    dl->AddRectFilled(ImVec2(lg_x - 13, lg_y + 44), ImVec2(lg_x - 5, lg_y + 50), IM_COL32(255, 0, 0, 255));

    ImGui::EndChild();
}

void Exporter_View::handleAnalyze(Editor &editor) {
    const size_t step = 8192;

    static bool is_analyzing = false;
    static bool show_stats = false;

    auto analyze = [&]() {
        std::vector<float> block(INNER_CHANNELS * step);

        uint64_t current_frame = export_range_.first;
        std::vector<audio_sample_t> frames(step*INNER_CHANNELS);
        uint64_t cur_block = 0;

        while (current_frame < export_range_.second) {

            // writing frames
            uint64_t frame_count = ((current_frame + step) >= export_range_.second ) ?
                                    export_range_.second - current_frame :
                                    step;

            frames.resize(frame_count * INNER_CHANNELS);
            editor.timeline.renderFrames(frames.data(), current_frame, frame_count);

            auto rms = calculateRMS2(frames.data(), frame_count);
            auto max_amp = calculateMax2(frames.data(), frame_count);

            block_stats_[0][cur_block] = {rms.first, max_amp.first};
            block_stats_[1][cur_block] = {rms.second, max_amp.second};

            current_frame += frame_count;
            cur_block++;
        }

        editor.playback_state.player.start();
        is_analyzing = false;
    };

    if (ImGui::Button("Analyze") && !is_analyzing) {
        const size_t block_cnt = (export_range_.second - export_range_.first + step - 1) / step;

        show_stats = true;
        block_stats_[0].clear();
        block_stats_[0].resize(block_cnt);

        block_stats_[1].clear();
        block_stats_[1].resize(block_cnt);

        editor.playback_state.player.stop();

        is_analyzing = true;

        std::thread analyze_thread(analyze);
        analyze_thread.detach();

    }

    ImGui::Checkbox("Show stats", &show_stats);

    if (show_stats) {
        ImGui::Text("Left channel");
        ID_GUARD(0, PlotAudioBlockStats(block_stats_[0], export_range_.first); );

        ImGui::Text("Right channel");
        ID_GUARD(1, PlotAudioBlockStats(block_stats_[1], export_range_.first); );
    }
}

void Exporter_View::Draw(Editor& editor) {

    handlePathChoose();

    ImGui::Separator();

    handleRangeChoose(editor.timeline.playhead_frame);

    handleExport(editor);

    handleAnalyze(editor);

}

}
