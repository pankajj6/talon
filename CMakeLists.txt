cmake_minimum_required(VERSION 3.20)
project(sim CXX)

# 1. CHANGE OPTIMIZATION LEVEL HERE
# Options: Debug, Release, RelWithDebInfo, MinSizeRel
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. OPTIONAL: Native hardware optimizations for maximum performance
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()

# ============================================================
# ADDRESS SANITIZER FLAGS (For debugging segfaults)
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Fixed syntax error quotes in your original snippet here
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# All headers live here — every target can find them
include_directories(
    infra/include
    base_lob_engine
    agents
)

# ============================================================
# infra library
# ============================================================
add_library(infra STATIC
    infra/src/priority_queue.cpp
)
target_include_directories(infra PUBLIC infra/include)

# ============================================================
# kernel executable
# ============================================================
add_executable(kernel kernel/main.cpp)
target_link_libraries(kernel infra)