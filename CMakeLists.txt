cmake_minimum_required(VERSION 3.10)
project(WebCraft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 1. Platform Dependencies ---
if (WIN32)
    set(WEBCRAFT_PLATFORM_LIBS ws2_32)
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(uring REQUIRED IMPORTED_TARGET liburing)
    set(WEBCRAFT_PLATFORM_LIBS PkgConfig::uring)
else()
    set(WEBCRAFT_PLATFORM_LIBS "")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(-fexperimental-library)
endif()

# --- 2. Build Target ---
file(GLOB_RECURSE WebCraft_SOURCES CONFIGURE_DEPENDS src/webcraft/*.cpp)
add_library(WebCraft STATIC ${WebCraft_SOURCES})

# Include directories: Local path for builds, "include/" for installs
target_include_directories(WebCraft PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(WebCraft PUBLIC ${WEBCRAFT_PLATFORM_LIBS})

# --- 3. Install Rules (Crucial for Port) ---
# Installs the binary
install(TARGETS WebCraft
    EXPORT WebCraftTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Installs the headers
install(DIRECTORY include/ DESTINATION include)

# Installs the CMake config (so users can find_package(WebCraft))
install(EXPORT WebCraftTargets
    FILE WebCraftTargets.cmake
    NAMESPACE WebCraft::
    DESTINATION share/WebCraft
)

# --- 4. Testing ---
option(WEBCRAFT_BUILD_TESTS "Build WebCraft tests" OFF)
if(WEBCRAFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()