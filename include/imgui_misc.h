#include "common.h"
#include "imgui.h"

struct InputTextCallback_UserData
{
    std::string*            Str;
    ImGuiInputTextCallback  ChainCallback;
    void*                   ChainCallbackUserData;
};

namespace ImGui {

IMGUI_API bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr);

// Save current cursor position on construction and restore it on destruction
struct CursorGuard {
    ImVec2 old_cursor_pos;
    CursorGuard(): old_cursor_pos(ImGui::GetCursorScreenPos()) {}
    ~CursorGuard() {ImGui::SetCursorScreenPos(old_cursor_pos); }
};

// Push id on construction and pop it on destruction
struct IdGuard {
    IdGuard(const void* id) { ImGui::PushID(id); }
    IdGuard(int id) { ImGui::PushID(id); }
    IdGuard(const char* id) { ImGui::PushID(id); }
    ~IdGuard()    { ImGui::PopID();}
};

#define ID_GUARD(id, __VA_ARGS__)   \
    do {                            \
    ImGui::IdGuard id_guard___(id); \
    __VA_ARGS__                     \
    } while(0)

}

static inline int InputTextCallback(ImGuiInputTextCallbackData* data)
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

inline bool ImGui::InputText(const char* label, std::string* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallback_UserData cb_user_data;
    cb_user_data.Str = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return InputText(label, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback, &cb_user_data);
}