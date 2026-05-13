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
IMGUI_API bool PlayStopWidget(ImVec2 size, bool is_playing);
// Save current cursor position on construction and restore it on destruction
struct CursorGuard {
    ImVec2 old_cursor_pos;
    // Just save current position
    CursorGuard(): old_cursor_pos(ImGui::GetCursorScreenPos()) {}
    // Save current position and set new
    CursorGuard(const ImVec2 &pos): CursorGuard() { ImGui::SetCursorScreenPos(pos); }
    ~CursorGuard() {ImGui::SetCursorScreenPos(old_cursor_pos); }
};

// Push id on construction and pop it on destruction
struct IdGuard {
    IdGuard(const void* id) { ImGui::PushID(id); }
    IdGuard(int id) { ImGui::PushID(id); }
    IdGuard(const char* id) { ImGui::PushID(id); }
    ~IdGuard()    { ImGui::PopID();}
};

#define ID_GUARD(id, ...)           \
    do {                            \
    ImGui::IdGuard id_guard___(id); \
    __VA_ARGS__                     \
    } while(0)

}
