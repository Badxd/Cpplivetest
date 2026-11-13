cmake_minimum_required(VERSION 3.20)
project(cppgram_live_test_bot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The CppGram library is the dependency under test. It is NOT modified by this project.
#   -DCPPGRAM_SOURCE_DIR=/path/to/cppgram            build the unmodified library from source (default: ../cppgram)
#   -DCPPGRAM_PREBUILT_DIR=/path/with/libcppgram.a   link an already built libcppgram.a instead
#   (the headers always come from CPPGRAM_SOURCE_DIR/include, or CPPGRAM_PREBUILT_INCLUDE_DIR when set)
set(CPPGRAM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cppgram" CACHE PATH "CppGram source tree (unmodified)")
set(CPPGRAM_PREBUILT_DIR "" CACHE PATH "Directory containing a prebuilt libcppgram.a (optional)")
set(CPPGRAM_PREBUILT_INCLUDE_DIR "" CACHE PATH "Include dir for a prebuilt library (defaults to SOURCE/include)")

if(NOT CPPGRAM_PREBUILT_DIR)
    if(NOT EXISTS "${CPPGRAM_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "CppGram source not found at ${CPPGRAM_SOURCE_DIR}. Set -DCPPGRAM_SOURCE_DIR=...")
    endif()
    set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_LIVE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_LIVE_CHECK OFF CACHE BOOL "" FORCE)
    set(ENABLE_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CPPGRAM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/cppgram" EXCLUDE_FROM_ALL)
    set(LIVEBOT_CPPGRAM_TARGET cppgram)
    set(LIVEBOT_CPPGRAM_INCLUDE "${CPPGRAM_SOURCE_DIR}/include")
else()
    add_library(cppgram_prebuilt STATIC IMPORTED)
    set_target_properties(cppgram_prebuilt PROPERTIES IMPORTED_LOCATION "${CPPGRAM_PREBUILT_DIR}/libcppgram.a")
    find_package(Boost REQUIRED)
    find_package(CURL REQUIRED)
    find_package(nlohmann_json 3.7.3 REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(SQLite3 REQUIRED)
    find_package(Threads REQUIRED)
    set_target_properties(cppgram_prebuilt PROPERTIES INTERFACE_LINK_LIBRARIES
        "Boost::headers;CURL::libcurl;nlohmann_json::nlohmann_json;Threads::Threads;OpenSSL::Crypto;ZLIB::ZLIB;SQLite::SQLite3")
    set(LIVEBOT_CPPGRAM_TARGET cppgram_prebuilt)
    if(CPPGRAM_PREBUILT_INCLUDE_DIR)
        set(LIVEBOT_CPPGRAM_INCLUDE "${CPPGRAM_PREBUILT_INCLUDE_DIR}")
    else()
        set(LIVEBOT_CPPGRAM_INCLUDE "${CPPGRAM_SOURCE_DIR}/include")
    endif()
endif()

find_package(OpenSSL REQUIRED)

add_library(livebot_core STATIC
    src/config.cpp
    src/log.cpp
    src/state.cpp
    src/commands.cpp
    src/test_runner.cpp
    src/tests.cpp
    src/proxy_profile.cpp
)
target_include_directories(livebot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${LIVEBOT_CPPGRAM_INCLUDE}")
target_compile_options(livebot_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(livebot_core PUBLIC ${LIVEBOT_CPPGRAM_TARGET} OpenSSL::Crypto)

add_executable(cppgram_live_test_bot src/main.cpp)
target_compile_options(cppgram_live_test_bot PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(cppgram_live_test_bot PRIVATE livebot_core)

enable_testing()
add_executable(livebot_unit_tests tests/unit_tests.cpp)
target_link_libraries(livebot_unit_tests PRIVATE livebot_core)
add_test(NAME livebot_unit_tests COMMAND livebot_unit_tests)
