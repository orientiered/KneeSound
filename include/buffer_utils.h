#pragma once

#include "common.h"
#include <mutex>

/* ====================== MULTI CHANNEL BUFFER ===================== */
// Used for storing deinterleaved samples
class MultiChannelBuffer {
    std::unique_ptr<float[]> memory;   // Using one contiguos block and
    std::vector<float*> channels;      // Array of pointers
    uint32_t channels_count_ = 0;
    uint32_t frame_count_ = 0;

public:
    uint32_t getChannels() const { return channels_count_; }
    uint32_t getFrameCount() const { return frame_count_; }

    MultiChannelBuffer() {}

    MultiChannelBuffer(uint32_t frame_count, uint32_t ch) {
        resize(frame_count, ch);
    }

    void resize(uint32_t size, uint32_t ch) {
        if (ch != channels_count_ || size != frame_count_) {
            channels_count_ = ch;
            frame_count_ = size;
            memory = std::make_unique<float[]>(ch * size);
            channels.resize(ch);
            for (uint32_t i = 0; i < ch; ++i) {
                channels[i] = memory.get() + i * size;
            }
        }
    }

    void clear() {
        std::fill(memory.get(), memory.get() + channels_count_ * frame_count_, 0);
    }

    bool empty() { return !memory.get(); }

    float* getChannel(uint32_t ch) {
        assert(ch < channels_count_);
        return channels[ch];
    }

    float getMeanSample(uint32_t frame) const {
        float sum = 0;
        for (int i = 0; i < channels_count_; i++) {
            sum += channels[i][frame];
        }

        return sum / channels_count_;

    }

    const float* operator[](uint32_t ch) const { return channels[ch]; }

    // Raw data
    const float * const *data() const {return channels.data(); }

    float** data() { return channels.data(); }
};

/* ====================== STREAMING BUFFER ========================= */
// Use case:
// Audio thread -> renders pack of frames and then uses them on next stage
// GUI thread   -> gets latest pack of rendered frames for visualization
// Overall data flow is controlled by audio thread

using AudioBuffer = MultiChannelBuffer;
struct ReadableStreamingBuffer {
private:
    AudioBuffer buffers[3];
    std::int16_t latest_buffer = 0;
    std::int16_t reader_buffer = 0;
    std::int16_t writer_buffer = -1;

    std::mutex& mtx;

public:
    ReadableStreamingBuffer(std::mutex& mtx_, size_t frame_count, size_t channels);
    /* =============== WRITER THREAD ========================== */

    // Get free buffer filled with zeros
    AudioBuffer &writerGetBuffer(size_t frame_count, size_t channels);
    // Use after writerGetBuffer to mark it as ready
    // It is guaranteed that this buffer will be read only at least until next getBuffer() call
    // Also this buffer won't be rewritten if reader took it
    const AudioBuffer &writerSentReadyBuffer();

    /* ============== READER THREAD =========================== */

    // Get latest buffer ready for processing
    // Also releases previously tooken buffer
    const AudioBuffer &readerGetReadyBuffer();

};


#include <vector>
#include <algorithm>
#include <cstddef>
#include <stdexcept>

/* ====================== BULK QUEUE ============================== */
// Queue optimized for bulk operations: push/pop N elems
// No reallocation
template<typename T>
class BulkQueue {
public:
    explicit BulkQueue(std::size_t capacity)
        : data_(capacity), capacity_(capacity) {
        if (capacity_ == 0) throw std::invalid_argument("Capacity must be > 0");
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool full() const noexcept { return count_ == capacity_; }

    void clear() noexcept {
        head_ = 0; tail_ = 0; count_ = 0;
    }

    /// @brief Bulk writing
    /// @return Number of elements pushed
    std::size_t push_bulk(const T* src, std::size_t count) {
        if (count == 0 || count_ == capacity_) return 0;
        std::size_t to_push = std::min(count, capacity_ - count_);

        std::size_t first_chunk = std::min(to_push, capacity_ - tail_);
        std::copy_n(src, first_chunk, data_.data() + tail_);
        tail_ = (tail_ + first_chunk) % capacity_;

        if (first_chunk < to_push) {
            std::size_t second_chunk = to_push - first_chunk;
            std::copy_n(src + first_chunk, second_chunk, data_.data());
            tail_ = second_chunk;
        }

        count_ += to_push;
        return to_push;
    }

    /// @brief Bulk reading
    /// @return Number of elements popped
    std::size_t pop_bulk(T* dst, std::size_t count) {
        if (count == 0 || count_ == 0) return 0;
        std::size_t to_pop = std::min(count, count_);

        std::size_t first_chunk = std::min(to_pop, capacity_ - head_);
        std::copy_n(data_.data() + head_, first_chunk, dst);
        head_ = (head_ + first_chunk) % capacity_;

        if (first_chunk < to_pop) {
            std::size_t second_chunk = to_pop - first_chunk;
            std::copy_n(data_.data(), second_chunk, dst + first_chunk);
            head_ = second_chunk;
        }

        count_ -= to_pop;
        return to_pop;
    }

private:
    std::vector<T> data_;
    std::size_t capacity_;
    std::size_t head_ = 0;  ///< Index of next element for reading
    std::size_t tail_ = 0;  ///< Index of next element for writing
    std::size_t count_ = 0; ///< Total elements stored
};

using AudioBlockAdapter = BulkQueue<audio_sample_t>;
