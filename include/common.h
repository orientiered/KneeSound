#pragma once

//! THIS HEADER MUST BE INCLUDED BEFORE IMGUI

// Using math operations for ImVec2
#define IMGUI_DEFINE_MATH_OPERATORS

/* ============== STL containers ================== */

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <list>
#include <optional>

/* =============== PLOG Logger headers ============ */

#include <plog/Log.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>


struct DebugFlags {
    bool debug_window = true; // show debug window

    bool render_loop_logs = false;
    bool callback_logs = false;
    bool block_adapter_logs = false;

    bool preview_new_waveform = false;
};

extern DebugFlags g_debug_flags;

/* =============== WAVES constants ================ */

namespace waves {

const int INNER_CHANNELS = 2;
const int INNER_SAMPLE_RATE = 48000;

const int RENDER_BLOCK_SIZE = 1024; ///< Size of block used across all rendering in frames

const char * const POOL_DND = "POOL_DND_TYPE";

}

using audio_sample_t = float;
