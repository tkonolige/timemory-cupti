cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(timemory-cupti LANGUAGES C CXX CUDA)

find_package(CUDAToolkit REQUIRED)
find_package(timemory REQUIRED COMPONENTS cxx cuda cupti papi)

add_library(timemory-cupti SHARED cupti_perfworks.cpp)
target_link_libraries(timemory-cupti PUBLIC timemory::timemory)

add_executable(main main.cu)
target_link_libraries(main PUBLIC timemory-cupti pthread CUDA::cudart CUDA::cupti ${CMAKE_DL_LIBS} m stdc++ PAPI::papi-shared)
