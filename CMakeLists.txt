cmake_minimum_required(VERSION 3.10)
project(kanzi LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_CXX_COMPILER "clang++")

# Set C standard (for TestAPI.c)
set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD_REQUIRED True)
#set(CMAKE_C_COMPILER "clang")

if(CONCURRENCY_DISABLED)
    add_definitions(-DCONCURRENCY_DISABLED)
endif()

set(COMMON_FLAGS "-Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -fno-rtti")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# Source files
set(LIB_COMMON_SOURCES
    ${SRC_DIR}/Global.cpp
    ${SRC_DIR}/Event.cpp
    ${SRC_DIR}/entropy/EntropyUtils.cpp
    ${SRC_DIR}/entropy/HuffmanCommon.cpp
    ${SRC_DIR}/entropy/CMPredictor.cpp
    ${SRC_DIR}/entropy/TPAQPredictor.cpp
    ${SRC_DIR}/transform/AliasCodec.cpp
    ${SRC_DIR}/transform/BWT.cpp
    ${SRC_DIR}/transform/BWTS.cpp
    ${SRC_DIR}/transform/DivSufSort.cpp
    ${SRC_DIR}/transform/SBRT.cpp
    ${SRC_DIR}/transform/BWTBlockCodec.cpp
    ${SRC_DIR}/transform/LZCodec.cpp
    ${SRC_DIR}/transform/FSDCodec.cpp
    ${SRC_DIR}/transform/ROLZCodec.cpp
    ${SRC_DIR}/transform/RLT.cpp
    ${SRC_DIR}/transform/SRT.cpp
    ${SRC_DIR}/transform/TextCodec.cpp
    ${SRC_DIR}/transform/UTFCodec.cpp
    ${SRC_DIR}/transform/EXECodec.cpp
    ${SRC_DIR}/transform/ZRLT.cpp
)

set(LIB_COMP_SOURCES
    ${SRC_DIR}/api/Compressor.cpp
    ${SRC_DIR}/bitstream/DebugOutputBitStream.cpp
    ${SRC_DIR}/bitstream/DefaultOutputBitStream.cpp
    ${SRC_DIR}/io/CompressedOutputStream.cpp
    ${SRC_DIR}/entropy/ANSRangeEncoder.cpp
    ${SRC_DIR}/entropy/BinaryEntropyEncoder.cpp
    ${SRC_DIR}/entropy/ExpGolombEncoder.cpp
    ${SRC_DIR}/entropy/FPAQEncoder.cpp
    ${SRC_DIR}/entropy/HuffmanEncoder.cpp
    ${SRC_DIR}/entropy/RangeEncoder.cpp
)

set(LIB_DECOMP_SOURCES
    ${SRC_DIR}/api/Decompressor.cpp
    ${SRC_DIR}/bitstream/DebugInputBitStream.cpp
    ${SRC_DIR}/bitstream/DefaultInputBitStream.cpp
    ${SRC_DIR}/io/CompressedInputStream.cpp
    ${SRC_DIR}/entropy/ANSRangeDecoder.cpp
    ${SRC_DIR}/entropy/BinaryEntropyDecoder.cpp
    ${SRC_DIR}/entropy/ExpGolombDecoder.cpp
    ${SRC_DIR}/entropy/FPAQDecoder.cpp
    ${SRC_DIR}/entropy/HuffmanDecoder.cpp
    ${SRC_DIR}/entropy/RangeDecoder.cpp
)

set(TEST_SOURCES
    ${SRC_DIR}/test/TestEntropyCodec.cpp
    ${SRC_DIR}/test/TestBWT.cpp
    ${SRC_DIR}/test/TestCompressedStream.cpp
    ${SRC_DIR}/test/TestDefaultBitStream.cpp
    ${SRC_DIR}/test/TestTransforms.cpp
    ${SRC_DIR}/test/TestAPI.cpp
)

set(APP_SOURCES
    ${SRC_DIR}/app/Kanzi.cpp
    ${SRC_DIR}/app/InfoPrinter.cpp
    ${SRC_DIR}/app/BlockCompressor.cpp
    ${SRC_DIR}/app/BlockDecompressor.cpp
)

# Libraries
add_library(libkanzi STATIC ${LIB_COMMON_SOURCES} ${LIB_COMP_SOURCES} ${LIB_DECOMP_SOURCES})
add_library(libkanzi_shared SHARED ${LIB_COMMON_SOURCES} ${LIB_COMP_SOURCES} ${LIB_DECOMP_SOURCES})
set_target_properties(libkanzi PROPERTIES OUTPUT_NAME "kanzi")
set_target_properties(libkanzi_shared PROPERTIES OUTPUT_NAME "kanzi")

#add_library(libkanzi_comp STATIC ${LIB_COMP_SOURCES})
#add_library(libkanzi_decomp STATIC ${LIB_DECOMP_SOURCES})

#add_library(libkanzi_comp_shared SHARED ${LIB_COMP_SOURCES})
#add_library(libkanzi_decomp_shared SHARED ${LIB_DECOMP_SOURCES})

# Executable target for C++
add_executable(testBWT ${SRC_DIR}/test/TestBWT.cpp)
target_link_libraries(testBWT libkanzi)

add_executable(testTransforms ${SRC_DIR}/test/TestTransforms.cpp)
target_link_libraries(testTransforms libkanzi)

add_executable(testEntropyCodec ${SRC_DIR}/test/TestEntropyCodec.cpp)
target_link_libraries(testEntropyCodec libkanzi)

add_executable(testDefaultBitStream ${SRC_DIR}/test/TestDefaultBitStream.cpp)
target_link_libraries(testDefaultBitStream libkanzi)

add_executable(testCompressedStream ${SRC_DIR}/test/TestCompressedStream.cpp)
target_link_libraries(testCompressedStream libkanzi)

# Executable target for C API test (TestAPI.c)
add_executable(testAPI ${SRC_DIR}/test/TestAPI.c)
target_link_libraries(testAPI libkanzi)
# IMPORTANT: Force use of C++ Linker because we link against C++ lib 'archon_static'
set_target_properties(testAPI PROPERTIES LINKER_LANGUAGE CXX)

# Main executable

# Dynamically linked executable
add_executable(kanzi ${APP_SOURCES})
target_link_libraries(kanzi libkanzi_shared)

# Set RPATH for Linux so it finds libkanzi.so at runtime
set_target_properties(kanzi PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib"
)

# Statically linked executable
add_executable(kanzi_static ${APP_SOURCES})
target_link_libraries(kanzi_static libkanzi)

# Custom target to build all tests (Named to avoid conflict with CTest 'make test')
add_custom_target(build_tests
    DEPENDS testBWT testTransforms testEntropyCodec testDefaultBitStream testCompressedStream testAPI
)

# --- CTest Configuration ---
enable_testing()

if(DEFINED ENV{TMPDIR})
    set(SYSTEM_TEMP_DIR "$ENV{TMPDIR}") # Linux/macOS standard
elseif(DEFINED ENV{TEMP})
    set(SYSTEM_TEMP_DIR "$ENV{TEMP}")   # Windows standard
elseif(DEFINED ENV{TMP})
    set(SYSTEM_TEMP_DIR "$ENV{TMP}")    # Windows fallback
else()
    set(SYSTEM_TEMP_DIR "/tmp")         # Fallback default
endif()

file(TO_CMAKE_PATH "${SYSTEM_TEMP_DIR}" SYSTEM_TEMP_DIR)

# Register executables as CTest tests
# Syntax: add_test(NAME <NameInReport> COMMAND <TargetName>)
add_test(NAME BWT COMMAND testBWT -noperf)
add_test(NAME Transforms COMMAND testTransforms -type=all -noperf)
add_test(NAME EntropyCodec COMMAND testEntropyCodec -type=all -noperf)
add_test(NAME DefaultBitStream COMMAND testDefaultBitStream ${SYSTEM_TEMP_DIR}/testDefaultBitStream.tmp -noperf)
add_test(NAME CompressedStream COMMAND testCompressedStream)
add_test(NAME API COMMAND testAPI)

# Custom target to build static libraries
add_custom_target(static_lib
    DEPENDS libkanzi #libkanzi_comp libkanzi_decomp
)

# Custom target to build shared libraries
add_custom_target(shared_lib
    DEPENDS libkanzi_shared #libkanzi_comp_shared libkanzi_decomp_shared
)

# Custom target to build all libraries (static and shared)
add_custom_target(lib
    DEPENDS static_lib shared_lib
)

# Install the statically linked executable
install(TARGETS kanzi_static
    RUNTIME DESTINATION bin
)

# Install dynamically linked executable
install(TARGETS kanzi
    RUNTIME DESTINATION bin
)

# Install the libraries (static and shared)
install(TARGETS libkanzi libkanzi_shared
    ARCHIVE DESTINATION lib    # For static libraries (.a, .lib)
    LIBRARY DESTINATION lib    # For shared libraries (.so, .dylib, .dll)
)

# Dynamically discover and install public headers
# We assume all header files (.h and .hpp) in the project directories
# (excluding test/ and app/ directories) are public headers.

# Define the base directories where headers might reside
# PROJECT_SOURCE_DIR refers to the directory containing CMakeLists.txt
set(HEADER_BASE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR} # For headers like Global.h, Event.h at root
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/bitstream
    ${CMAKE_CURRENT_SOURCE_DIR}/entropy
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/transform
)

# List to hold all found header files
set(ALL_HEADERS)

# Iterate through the base directories and find headers
foreach(dir IN LISTS HEADER_BASE_DIRS)
    file(GLOB_RECURSE CURRENT_DIR_HEADERS "${dir}/*.h" "${dir}/*.hpp")
    list(APPEND ALL_HEADERS ${CURRENT_DIR_HEADERS})
endforeach()

# Install each header, preserving its relative path within the 'kanzi' include directory
foreach(header_file IN LISTS ALL_HEADERS)
    # Calculate the relative path from the project root
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${header_file})

    # Install to include/kanzi/ followed by its relative path
    install(FILES "${header_file}"
        DESTINATION "include/kanzi/${REL_PATH}"
    )
endforeach()

# Uninstall all files added during install
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstalling..."
)
