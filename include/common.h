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
    bool render_loop_logs = false;
    bool callback_logs = false;
};

extern DebugFlags g_debug_flags;

/* =============== WAVES constants ================ */

namespace waves {

const int INNER_CHANNELS = 2;
const int INNER_SAMPLE_RATE = 48000;

const char * const POOL_DND = "POOL_DND_TYPE";

}

using audio_sample_t = float;
