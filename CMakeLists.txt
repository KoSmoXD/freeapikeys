cmake_minimum_required(VERSION 3.21)
project(TrajectoryAutoPlayer VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
# Geode 5.5.3's Android asp2 dependency omits <type_traits> in a std23 header.
# Preinclude it across the build to avoid the NDK r28 compilation failure.
if(ANDROID)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include type_traits")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to the installed Geode SDK directory")
endif()
# Geode SDK v5.5.3 has a duplicate CCFileUtilsWin32::init declaration.
# Work around this upstream header issue in the CI SDK copy (not the repository).
set(GEODE_FILEUTILS_HEADER "$ENV{GEODE_SDK}/loader/include/Geode/cocos/platform/win32/CCFileUtilsWin32.h")
if(EXISTS "${GEODE_FILEUTILS_HEADER}")
    file(READ "${GEODE_FILEUTILS_HEADER}" GEODE_FILEUTILS_CONTENT)
    string(REPLACE "    virtual bool init();\n\n    /**" "    /**"
        GEODE_FILEUTILS_FIXED "${GEODE_FILEUTILS_CONTENT}")
    if(NOT GEODE_FILEUTILS_FIXED STREQUAL GEODE_FILEUTILS_CONTENT)
        file(WRITE "${GEODE_FILEUTILS_HEADER}" "${GEODE_FILEUTILS_FIXED}")
        message(STATUS "Patched duplicate Geode SDK CCFileUtilsWin32::init declaration")
    endif()
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
