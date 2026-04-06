cmake_minimum_required(VERSION 3.26)
project(AuEd
  LANGUAGES CXX
  VERSION 0.1
)

add_subdirectory(lib_deps)
# add_subdirectory(src)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EXEC_NAME editor.exe)

option(ENABLE_ASAN "Enable Address Sanitizer" ON)

if(ENABLE_ASAN)
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


set(PROJ_SRC src/main.cpp src/miniaudio.cpp src/editor.cpp src/timeline_view.cpp src/timeline.cpp src/media_pool_view.cpp)

add_executable(${EXEC_NAME} ${PROJ_SRC} lib_deps/ImGuiFileDialog/ImGuiFileDialog.cpp)
target_include_directories(${EXEC_NAME} PRIVATE include lib_deps/ImGuiFileDialog)
target_link_libraries(${EXEC_NAME} PRIVATE ImGui-SFML::ImGui-SFML plog::plog)

# Copy DLLs needed for runtime on Windows
if(WIN32)
  if (BUILD_SHARED_LIBS)
    add_custom_command(TARGET ${EXEC_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:ImGui-SFML::ImGui-SFML>
          $<TARGET_FILE:sfml-graphics>
          $<TARGET_FILE:sfml-window>
          $<TARGET_FILE:sfml-system>
          $<TARGET_FILE_DIR:example_exe>)
  endif()
endif()
