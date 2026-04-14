#pragma once

#include "common.h"
#include <mutex>

/* ====================== STREAMING BUFFER ========================= */
// Use case: 
// Audio thread -> renders pack of frames and then uses them on next stage
// GUI thread   -> gets latest pack of rendered frames for visualization
// Overall data flow is controlled by audio thread
struct ReadableStreamingBuffer {
private:
    std::vector<audio_sample_t> buffers[3];
    std::int16_t latest_buffer = 0;
    std::int16_t reader_buffer = 0; 
    std::int16_t writer_buffer = -1; 

    std::mutex& mtx;

public:
    ReadableStreamingBuffer(std::mutex& mtx_, size_t elems);
    /* =============== WRITER THREAD ========================== */

    // Get free buffer filled with zeros
    std::vector<audio_sample_t> &writerGetBuffer(size_t elems);
    // Use after writerGetBuffer to mark it as ready 
    // It is guaranteed that this buffer will be read only at least until next getBuffer() call
    // Also this buffer won't be rewritten if reader took it
    const std::vector<audio_sample_t> &writerSentReadyBuffer();

    /* ============== READER THREAD =========================== */

    // Get latest buffer ready for processing
    // Also releases previously tooken buffer
    const std::vector<audio_sample_t> &readerGetReadyBuffer();

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