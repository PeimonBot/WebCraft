# WebCraft examples
cmake_minimum_required(VERSION 3.10)

# HTTP server example
add_executable(http_server http_server/main.cpp)
target_link_libraries(http_server PRIVATE WebCraft)
target_include_directories(http_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
