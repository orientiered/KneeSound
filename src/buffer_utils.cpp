#include "buffer_utils.h"

/* ================= ReadableStreamingBuffer ==== */
ReadableStreamingBuffer::ReadableStreamingBuffer(std::mutex& mtx_, size_t elems): 
    mtx(mtx_) 
{
    buffers[0].resize(elems);
    buffers[1].resize(elems);
    buffers[2].resize(elems);
}

std::vector<audio_sample_t> &ReadableStreamingBuffer::writerGetBuffer(size_t elems) {
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

    std::vector<audio_sample_t> &buf = buffers[writer_buffer];
    buf.resize(elems);
    std::fill(buf.begin(), buf.end(), 0);

    return buf;
}

const std::vector<audio_sample_t> &ReadableStreamingBuffer::writerSentReadyBuffer() {
    std::lock_guard<std::mutex> lock(mtx);

    latest_buffer = writer_buffer;
    writer_buffer = -1;

    return buffers[latest_buffer];
}

const std::vector<audio_sample_t> &ReadableStreamingBuffer::readerGetReadyBuffer() {
    std::lock_guard<std::mutex> lock(mtx);

    reader_buffer = latest_buffer;

    return buffers[reader_buffer];
}