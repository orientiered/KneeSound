#include "imgui_misc.h"
#include <imgui_internal.h>

static int InputTextCallback(ImGuiInputTextCallbackData* data)
{
    InputTextCallback_UserData* user_data = (InputTextCallback_UserData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
        std::string* str = user_data->Str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = (char*)str->c_str();
    }
    else if (user_data->ChainCallback)
    {
        // Forward to user callback, if any
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}

bool ImGui::InputText(const char* label, std::string* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return InputText(label, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback, &cb_user_data);
}


bool ImGui::PlayStopWidget(ImVec2 size, bool is_playing) {

    ImGui::IdGuard ig("PlayStopWidget");

    // 1. Create the click target area (InvisibleButton)
    // This handles the interaction logic (hover, click)
    bool pressed = ImGui::InvisibleButton("PlayStopButtonArea", size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p_min = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    // 2. Determine Colors and State
    ImU32 background_color = ImGui::GetColorU32(ImGuiCol_Button);
    ImU32 highlight_color = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    ImU32 active_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);

    auto col_lerp = [](ImU32 a, ImU32 b, float t) {
        ImVec4 a4 = ImGui::ColorConvertU32ToFloat4(a);
        ImVec4 b4 = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImLerp(a4, b4, t));
    };

    // Apply interaction colors (Shadow/Focus effect)
    if (active) {
        background_color = col_lerp(background_color, active_color, 0.5f);
    } else if (hovered) {
        background_color = col_lerp(background_color, highlight_color, 0.3f);
    }

    // 3. Draw the Outer Button Shape
    draw_list->AddRectFilled(p_min, p_max, background_color, 10.0);

    // 4. Draw the Internal Icon
    ImVec2 icon_size = size * ImVec2(0.5, 0.63);
    ImVec2 icon_pos = p_min + (size - icon_size) / 2;

    if (!is_playing) {
        // --- PLAY Icon (Triangle) ---
        draw_list->AddTriangleFilled(
            icon_pos,
            icon_pos + ImVec2(icon_size.x, icon_size.y / 2),
            icon_pos + ImVec2(0, icon_size.y),
            IM_COL32(10, 124, 79, 200) // Green
        );
    } else {
        // --- Pause icon ( || ) ---
        ImVec2 pause_bar_size = icon_size * ImVec2(0.33, 1);
        ImVec2 sec_pos = icon_pos + icon_size * ImVec2(0.66, 0);
        draw_list->AddRectFilled(
            icon_pos, icon_pos + pause_bar_size,
            IM_COL32(140, 35, 24, 255) // Red
        );
        draw_list->AddRectFilled(
            sec_pos, sec_pos + pause_bar_size,
            IM_COL32(140, 35, 24, 255) // Red
        );
    }

    // 5. Return the click state
    return pressed;
}
