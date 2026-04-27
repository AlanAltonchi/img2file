cmake_minimum_required(VERSION 3.20)
project(img2file LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(img2file WIN32 src/main.cpp)
target_link_libraries(img2file PRIVATE
    gdiplus ole32 shell32 shlwapi oleaut32 uuid psapi
)

# Statically link libgcc/libstdc++/winpthread so the exe has no MinGW DLL deps
target_link_options(img2file PRIVATE -static -static-libgcc -static-libstdc++)

# Strip symbols in release for smallest binary
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(img2file PRIVATE -s)
endif()
