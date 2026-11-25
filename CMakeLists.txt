cmake_minimum_required(VERSION 3.20)
project(sdf_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

add_executable(sdf_engine
    src/main.cpp
    src/shader.cpp
)

target_include_directories(sdf_engine PRIVATE src)
target_link_libraries(sdf_engine PRIVATE SDL3::SDL3)

if(EMSCRIPTEN)
    set_target_properties(sdf_engine PROPERTIES SUFFIX ".html")
    target_link_options(sdf_engine PRIVATE
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file" "${CMAKE_SOURCE_DIR}/shaders@/shaders"
        "--shell-file" "${CMAKE_SOURCE_DIR}/web/shell.html"
        "-sEXPORTED_FUNCTIONS=[\"_main\",\"_sdf_set_scene\",\"_sdf_toggle_pause\",\"_sdf_reset_camera\"]"
        "-sEXPORTED_RUNTIME_METHODS=[\"ccall\"]"
    )
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(sdf_engine PRIVATE OpenGL::GL)
    add_custom_command(TARGET sdf_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:sdf_engine>/shaders"
    )
endif()
