cmake_minimum_required(VERSION 3.10)
project(WebCraft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(CPP_ASYNC_INCLUDE_DIRS "async/task.h")
if (NOT CPP_ASYNC_INCLUDE_DIRS)
    set(CPP_ASYNC_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}build/vcpkg_installed/arm64-osx/include")
endif()

if (WIN32)
    set(WEBCRAFT_PLATFORM_LIBS wsock32 ws2_32)
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(uring REQUIRED IMPORTED_TARGET liburing)
    set(WEBCRAFT_PLATFORM_LIBS PkgConfig::uring)
elseif (UNIX AND APPLE)
    # For macOS, you might want to link against other libraries or frameworks
    # For example, if you need to use libdispatch or other system libraries
    set(WEBCRAFT_PLATFORM_LIBS "")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()


file(GLOB_RECURSE WebCraft_SOURCES CONFIGURE_DEPENDS src/webcraft/*.cpp)
add_library(WebCraft STATIC ${WebCraft_SOURCES})

target_include_directories(WebCraft PUBLIC ${CPP_ASYNC_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(WebCraft PUBLIC ${WEBCRAFT_PLATFORM_LIBS})

enable_testing()
add_subdirectory(tests)