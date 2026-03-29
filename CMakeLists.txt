cmake_minimum_required(VERSION 3.10)

set(KANZI_PROJECT_VERSION "2.5.1")
set(KANZI_ABI_VERSION "1.0.0")
project(kanzi VERSION ${KANZI_PROJECT_VERSION} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

string(REPLACE "." ";" KANZI_ABI_VERSION_PARTS "${KANZI_ABI_VERSION}")
list(GET KANZI_ABI_VERSION_PARTS 0 KANZI_SOVERSION)

option(KANZI_ENABLE_NATIVE_OPTIMIZATIONS "Enable CPU-specific optimizations such as -march=native" ON)

set(KANZI_INSTALL_RELATIVE_RPATH_DEFAULT ON)

if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
    set(KANZI_INSTALL_RELATIVE_RPATH_DEFAULT OFF)
endif()

option(KANZI_INSTALL_RELATIVE_RPATH
    "Install the CLI with a relative runtime search path for the shared library"
    ${KANZI_INSTALL_RELATIVE_RPATH_DEFAULT}
)

set(KANZI_CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Kanzi")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#set(CMAKE_CXX_COMPILER "clang++")

# Set C standard (for TestAPI.c)
set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD_REQUIRED True)
#set(CMAKE_C_COMPILER "clang")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
# ----------------------------------------------

if(CONCURRENCY_DISABLED)
    add_definitions(-DCONCURRENCY_DISABLED)
endif()

if(MSVC)
    set(COMMON_FLAGS "/W4 /O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} /GR-")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
else()
    set(COMMON_FLAGS "-Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic")

    if(KANZI_ENABLE_NATIVE_OPTIMIZATIONS)
        string(APPEND COMMON_FLAGS " -march=native")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -fno-rtti")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
endif()
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Source files
set(LIB_COMMON_SOURCES
    ${SRC_DIR}/Global.cpp
    ${SRC_DIR}/Event.cpp
    ${SRC_DIR}/util/WallTimer.cpp
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

# This ensures -lpthread or -pthread is added to any executable linking these libs
target_link_libraries(libkanzi PUBLIC Threads::Threads)
target_link_libraries(libkanzi_shared PUBLIC Threads::Threads)

target_include_directories(libkanzi
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/kanzi>
)

target_include_directories(libkanzi_shared
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/kanzi>
)

set_target_properties(libkanzi PROPERTIES OUTPUT_NAME "kanzi")
set_target_properties(libkanzi_shared PROPERTIES
    OUTPUT_NAME "kanzi"
    VERSION "${KANZI_ABI_VERSION}"
    SOVERSION "${KANZI_SOVERSION}"
)

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

add_executable(testFactories ${SRC_DIR}/test/TestFactories.cpp)
target_link_libraries(testFactories libkanzi)

add_executable(testMalformedStream ${SRC_DIR}/test/TestMalformedStream.cpp)
target_link_libraries(testMalformedStream libkanzi)

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

# Keep the CLI runnable from relocatable prefixes, but avoid embedding an
# RPATH for standard system package installs.
if(UNIX AND NOT APPLE AND KANZI_INSTALL_RELATIVE_RPATH)
    set_target_properties(kanzi PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
endif()

# Statically linked executable
add_executable(kanzi_static ${APP_SOURCES})
target_link_libraries(kanzi_static libkanzi)

# Custom target to build all tests (Named to avoid conflict with CTest 'make test')
add_custom_target(build_tests
    DEPENDS testBWT testTransforms testEntropyCodec testDefaultBitStream testFactories testMalformedStream testCompressedStream testAPI
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
add_test(NAME Factories COMMAND testFactories)
add_test(NAME MalformedStream COMMAND testMalformedStream)
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
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

# Install dynamically linked executable
install(TARGETS kanzi
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

# Install the man page (prefer pre-compressed source file).
set(KANZI_MANPAGE_GZ "${CMAKE_CURRENT_SOURCE_DIR}/doc/kanzi.1.gz")
set(KANZI_MANPAGE "${CMAKE_CURRENT_SOURCE_DIR}/doc/kanzi.1")
set(KANZI_MANPAGE_TO_INSTALL "")

if(EXISTS "${KANZI_MANPAGE_GZ}")
    set(KANZI_MANPAGE_TO_INSTALL "${KANZI_MANPAGE_GZ}")
elseif(EXISTS "${KANZI_MANPAGE}")
    find_program(GZIP_EXECUTABLE gzip)

    if(GZIP_EXECUTABLE)
        set(KANZI_MANPAGE_GZ_BUILD "${CMAKE_CURRENT_BINARY_DIR}/kanzi.1.gz")
        execute_process(
            COMMAND "${GZIP_EXECUTABLE}" -n -c "${KANZI_MANPAGE}"
            OUTPUT_FILE "${KANZI_MANPAGE_GZ_BUILD}"
            RESULT_VARIABLE KANZI_GZIP_RESULT
        )

        if(NOT KANZI_GZIP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to gzip man page: ${KANZI_MANPAGE}")
        endif()

        set(KANZI_MANPAGE_TO_INSTALL "${KANZI_MANPAGE_GZ_BUILD}")
    else()
        message(WARNING "gzip not found. Installing uncompressed man page.")
        set(KANZI_MANPAGE_TO_INSTALL "${KANZI_MANPAGE}")
    endif()
endif()

if(KANZI_MANPAGE_TO_INSTALL)
    install(FILES "${KANZI_MANPAGE_TO_INSTALL}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
        COMPONENT Runtime
    )
endif()

# Install the libraries (static and shared)
install(TARGETS libkanzi libkanzi_shared
    EXPORT KanziTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Runtime NAMELINK_COMPONENT Development
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kanzi"
)

# Dynamically discover and install public headers
# We only install headers from the library tree, preserving paths relative to
# src/ so includes like <kanzi/api/Compressor.hpp> and <kanzi/util/WallTimer.hpp>
# remain stable across platforms.
set(PUBLIC_HEADER_BASE_DIRS
    ${SRC_DIR}
    ${SRC_DIR}/api
    ${SRC_DIR}/bitstream
    ${SRC_DIR}/entropy
    ${SRC_DIR}/io
    ${SRC_DIR}/transform
    ${SRC_DIR}/util
)

# List to hold all found header files
set(ALL_PUBLIC_HEADERS)

# Iterate through the base directories and find headers
foreach(dir IN LISTS PUBLIC_HEADER_BASE_DIRS)
    if(EXISTS "${dir}")
        file(GLOB_RECURSE CURRENT_DIR_HEADERS "${dir}/*.h" "${dir}/*.hpp")
        list(APPEND ALL_PUBLIC_HEADERS ${CURRENT_DIR_HEADERS})
    endif()
endforeach()

list(REMOVE_DUPLICATES ALL_PUBLIC_HEADERS)

# Install each header, preserving its relative path within the 'kanzi' include directory
foreach(header_file IN LISTS ALL_PUBLIC_HEADERS)
    file(RELATIVE_PATH REL_PATH "${SRC_DIR}" "${header_file}")

    if(NOT REL_PATH MATCHES "^(app|test|obj|build)/")
        get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY)

        if(REL_DIR STREQUAL ".")
            set(HEADER_DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kanzi")
        else()
            set(HEADER_DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/kanzi/${REL_DIR}")
        endif()

        install(FILES "${header_file}"
            DESTINATION "${HEADER_DESTINATION}"
            COMPONENT Development
        )
    endif()
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT Runtime
    )
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/KanziConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/KanziConfig.cmake"
    INSTALL_DESTINATION "${KANZI_CMAKE_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KanziConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(EXPORT KanziTargets
    FILE KanziTargets.cmake
    NAMESPACE kanzi::
    DESTINATION "${KANZI_CMAKE_INSTALL_CMAKEDIR}"
    COMPONENT Development
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KanziConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KanziConfigVersion.cmake"
    DESTINATION "${KANZI_CMAKE_INSTALL_CMAKEDIR}"
    COMPONENT Development
)

# Uninstall all files added during install
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstalling..."
)
