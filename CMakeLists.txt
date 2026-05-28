cmake_minimum_required(VERSION 3.20)
project(sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

# All headers live here — every target can find them
include_directories(
    infra/include
  )
   #agents

# ============================================================
# infra library — lob.cpp, engine.cpp, priority_queue.cpp
# kernel and experiments link against this
# ============================================================
add_library(infra STATIC
    infra/src/lob.cpp
    infra/src/engine.cpp
    infra/src/priority_queue.cpp
)
target_include_directories(infra PUBLIC infra/include)

# ============================================================
# kernel executable
# ============================================================
#add_executable(kernel kernel/main.cpp)
#target_link_libraries(kernel infra)

# ============================================================
# experiments — add one block per experiment
# ============================================================
#add_executable(exp_01 experiments/exp_01_basic_run/main.cpp)
#target_link_libraries(exp_01 infra)

add_executable(exp_02 experiments/exp_02_basic_run/main.cpp)
target_link_libraries(exp_02 infra)