include(FetchContent)

# SFML
FetchContent_Declare(
  sfml
  URL https://github.com/SFML/SFML/archive/refs/tags/3.0.2.zip
  URL_MD5 6efb446ac7c7546b832092f98e1a79a8
)
add_subdirectory(sfml)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG v1.91.9b-docking # 1.91.9b
)

FetchContent_MakeAvailable(imgui)

# ImGui-SFML
FetchContent_Declare(
  imgui-sfml
  GIT_REPOSITORY https://github.com/SFML/imgui-sfml
  GIT_TAG ab6b5cfeddb4eb980a42b526dc66901a0976ff6d # latest commit on 25.02.2026
)
add_subdirectory(imgui-sfml)

# Miniaudio
file(DOWNLOAD "https://raw.githubusercontent.com/mackron/miniaudio/refs/heads/master/miniaudio.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio/miniaudio.h"
)

# Plog (logging library)
add_subdirectory(plog)
