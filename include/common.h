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


namespace waves {

const int INNER_CHANNELS = 2;
const int INNER_SAMPLE_RATE = 48000;

}

using audio_sample_t = float;
