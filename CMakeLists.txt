cmake_minimum_required(VERSION 3.26)
project(AuEd
  LANGUAGES CXX
  VERSION 0.2
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# normal variables have higher priority than options
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_subdirectory(lib_deps)
# add_subdirectory(src)

set(EXEC_NAME KneeSound)

option(ENABLE_ASAN "Enable Address Sanitizer" ON)

option(INCLUDE_DEBUG_SYMBOLS "Include debug symbols" OFF)

if (INCLUDE_DEBUG_SYMBOLS)
  add_compile_options("-g2")
endif()

if(ENABLE_ASAN AND NOT WIN32)
  set(SAN_FLAGS "-fsanitize=address,undefined")
  add_compile_options(${SAN_FLAGS} -fno-omit-frame-pointer)
  add_link_options(${SAN_FLAGS})
endif()


# Copying SFML headers
set(SFML_DOWNLOAD_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/_deps/sfml-src/include/)

file(COPY ${SFML_DOWNLOAD_INCLUDE}
     DESTINATION ../include)

# Copying Miniaudio
file(COPY lib_deps/miniaudio/miniaudio.h
     DESTINATION ../include)

file(GLOB_RECURSE PROJ_SRC src/*.cpp)
message("Project sources: ${PROJ_SRC}")

add_executable(${EXEC_NAME} ${PROJ_SRC} lib_deps/ImGuiFileDialog/ImGuiFileDialog.cpp)
target_include_directories(${EXEC_NAME} PRIVATE include lib_deps/ImGuiFileDialog)
target_link_libraries(${EXEC_NAME} PRIVATE ImGui-SFML::ImGui-SFML plog::plog kissfft nlohmann_json::nlohmann_json)

# Copy DLLs needed for runtime on Windows
if(WIN32)
  if (BUILD_SHARED_LIBS)
    add_custom_command(TARGET ${EXEC_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:ImGui-SFML::ImGui-SFML>
          $<TARGET_FILE:sfml-graphics>
          $<TARGET_FILE:sfml-window>
          $<TARGET_FILE:sfml-system>
          $<TARGET_FILE:kissfft>
          $<TARGET_FILE_DIR:${EXEC_NAME}>)
  endif()
endif()
