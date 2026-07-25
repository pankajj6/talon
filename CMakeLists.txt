cmake_minimum_required(VERSION 3.20)
project(sim CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# ADDRESS SANITIZER FLAGS (For debugging segfaults)
# Remove or comment these out when running for performance/production!
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
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
# infra library — lob.cpp, engine.cpp, priority_queue.cpp
# kernel and experiments link against this
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

# ============================================================
# experiments — add one block per experiment
# ============================================================
#add_executable(exp_01 experiments/exp_01_basic_run/main.cpp)
#target_link_libraries(exp_01 infra)