cmake_minimum_required(VERSION 3.24)
project(iidxfsms LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
option(IIDXFSMS_BUILD_DLL "Build the Windows hook DLL" ${WIN32})

FetchContent_Declare(simpleini
    GIT_REPOSITORY https://github.com/brofield/simpleini.git
    GIT_TAG fd6db69efc40a687bf4ef81486b54066e34992dd)
FetchContent_MakeAvailable(simpleini)

add_library(iidxfsms_core STATIC src/config.cpp)
target_include_directories(iidxfsms_core PUBLIC src)
target_compile_definitions(iidxfsms_core PUBLIC SI_NO_CONVERSION)
target_link_libraries(iidxfsms_core PUBLIC SimpleIni::SimpleIni)
if(MINGW)
    add_link_options(-static)
endif()

if(IIDXFSMS_BUILD_DLL)
    if(NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Build the DLL using the x64 MinGW toolchain (build_docker.bat).")
    endif()
    FetchContent_Declare(minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537)
    FetchContent_MakeAvailable(minhook)
    add_library(iidxfsms SHARED src/iidxfsms.cpp src/game_hooks.cpp)
    target_include_directories(iidxfsms PRIVATE external/spice-sdk)
    target_compile_definitions(iidxfsms PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(iidxfsms PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(iidxfsms PRIVATE iidxfsms_core minhook)
    set_target_properties(iidxfsms PROPERTIES PREFIX "")
    add_custom_command(TARGET iidxfsms POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/iidxfsms.ini $<TARGET_FILE_DIR:iidxfsms>/iidxfsms.ini)
endif()