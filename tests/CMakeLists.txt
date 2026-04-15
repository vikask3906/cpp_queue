cmake_minimum_required(VERSION 3.20)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

find_package(Threads REQUIRED)

# ── helper macro ──────────────────────────────────────────────────────────────
# Usage: add_tsq_test(NAME test_foo SOURCES foo.cpp LABELS "concurrency;mpmc")
macro(add_tsq_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LABELS" ${ARGN})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_include_directories(${ARG_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${ARG_NAME} PRIVATE GTest::gtest_main Threads::Threads)
    target_compile_options(${ARG_NAME} PRIVATE -Wall -Wextra -Wno-interference-size)
    gtest_discover_tests(${ARG_NAME}
        PROPERTIES LABELS "${ARG_LABELS}"
        DISCOVERY_TIMEOUT 60
    )
endmacro()

# ── blocking MPMC unbounded ───────────────────────────────────────────────────
add_tsq_test(NAME test_blocking_mpmc_seq
    SOURCES blocking_mpmc/test_sequential.cpp
    LABELS  "sequential;mpmc;blocking")

add_tsq_test(NAME test_blocking_mpmc_conc
    SOURCES blocking_mpmc/test_concurrent.cpp
    LABELS  "concurrency;mpmc;blocking")

add_tsq_test(NAME test_blocking_mpmc_edge
    SOURCES blocking_mpmc/test_edge.cpp
    LABELS  "edge;mpmc;blocking")

# ── lock-free SPSC bounded ───────────────────────────────────────────────────
add_tsq_test(NAME test_lf_spsc_bounded_seq
    SOURCES lockfree_spsc_bounded/test_sequential.cpp
    LABELS  "sequential;spsc;lockfree;bounded")

add_tsq_test(NAME test_lf_spsc_bounded_conc
    SOURCES lockfree_spsc_bounded/test_concurrent.cpp
    LABELS  "concurrency;spsc;lockfree;bounded")

add_tsq_test(NAME test_lf_spsc_bounded_edge
    SOURCES lockfree_spsc_bounded/test_edge.cpp
    LABELS  "edge;spsc;lockfree;bounded")

# ── lock-free SPSC unbounded ─────────────────────────────────────────────────
add_tsq_test(NAME test_lf_spsc_unbounded_seq
    SOURCES lockfree_spsc_unbounded/test_sequential.cpp
    LABELS  "sequential;spsc;lockfree;unbounded")

add_tsq_test(NAME test_lf_spsc_unbounded_conc
    SOURCES lockfree_spsc_unbounded/test_concurrent.cpp
    LABELS  "concurrency;spsc;lockfree;unbounded")
