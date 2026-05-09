#include "utils/buffer_utils.h"

/* ================= ReadableStreamingBuffer ==== */
ReadableStreamingBuffer::ReadableStreamingBuffer(std::mutex& mtx_, size_t frame_count, size_t channels):
    mtx(mtx_)
{
    buffers[0].resize(frame_count, channels);
    buffers[1].resize(frame_count, channels);
    buffers[2].resize(frame_count, channels);
}

AudioBuffer &ReadableStreamingBuffer::writerGetBuffer(size_t frame_count, size_t channels) {
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (writer_buffer >= 0) {
            PLOG_ERROR << "Writer must release buffer first\n"
                    << "Assuming writer finished, marking it as latest buffer";
            latest_buffer = writer_buffer;
        }

        // searching buffer that is not taken by reader and is not latest
        for (int16_t i = 0; i < 3; i++) {
            if (latest_buffer != i && reader_buffer != i) {
                writer_buffer = i;
                break;
            }
        }
    }

    AudioBuffer &buf = buffers[writer_buffer];
    buf.resize(frame_count, channels);
    buf.clear();

    return buf;
}

const AudioBuffer &ReadableStreamingBuffer::writerSentReadyBuffer() {
    std::lock_guard<std::mutex> lock(mtx);

    latest_buffer = writer_buffer;
    writer_buffer = -1;

    return buffers[latest_buffer];
}

const AudioBuffer &ReadableStreamingBuffer::readerGetReadyBuffer() {
    std::lock_guard<std::mutex> lock(mtx);

    reader_buffer = latest_buffer;

    return buffers[reader_buffer];
}
