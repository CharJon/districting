cmake_minimum_required(VERSION 3.3)

project(districting CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# BLAS
find_package(BLAS REQUIRED)

# a CMake module named "FindGUROBI.cmake" is available in cmake/modules/
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

find_package(GUROBI REQUIRED)
include_directories(${GUROBI_INCLUDE_DIRS})

add_subdirectory(src)
set(SOURCES
        src/graph.cpp
        src/lagrange.cpp
        src/io.cpp
        src/hess.cpp
        src/flow.cpp
        src/cut.cpp
        src/ralg.cpp)

# EXECUTABLES
add_executable(districting
        src/main.cpp
        ${SOURCES}
        )

target_include_directories(districting
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(districting
        optimized ${GUROBI_CXX_LIBRARY}
        debug ${GUROBI_CXX_DEBUG_LIBRARY}
        ${GUROBI_LIBRARY}
        ${BLAS_LIBRARIES})


add_executable(ralg_hot_start
        src/main_hot_start.cpp
        ${SOURCES}
        )

target_include_directories(ralg_hot_start
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(ralg_hot_start
        optimized ${GUROBI_CXX_LIBRARY}
        debug ${GUROBI_CXX_DEBUG_LIBRARY}
        ${GUROBI_LIBRARY}
        ${BLAS_LIBRARIES})


add_executable(translate
        src/translate.cpp
        ${SOURCES}
        )

target_include_directories(translate
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(translate
        optimized ${GUROBI_CXX_LIBRARY}
        debug ${GUROBI_CXX_DEBUG_LIBRARY}
        ${GUROBI_LIBRARY}
        ${BLAS_LIBRARIES})


add_executable(gridgen
        src/gridgen.cpp
        ${SOURCES}
        )

target_include_directories(gridgen
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(gridgen
        optimized ${GUROBI_CXX_LIBRARY}
        debug ${GUROBI_CXX_DEBUG_LIBRARY}
        ${GUROBI_LIBRARY}
        ${BLAS_LIBRARIES})

# tests
option(TESTS "Build the tests" OFF)
if (TESTS)
    enable_testing()
    add_subdirectory(extern/googletest)
    include(GoogleTest)
    add_subdirectory(src/test)
endif ()
