/*
Copyright 2011-2026 Frederic Langlet
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
you may obtain a copy of the License at

                http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once
#ifndef knz_util
#define knz_util


#include <iostream>
#include <sstream>
#include "types.hpp"


// Ahem ... Visual Studio
// This code is required because Microsoft cannot bother to implement streambuf::pubsetbuf().
// Also On libstdc++, pubsetbuf() silently ignores the supplied buffer and leaves internal pointers null.

class ifixedbuf : public std::streambuf {
public:
    ifixedbuf(char* data, std::size_t size) {
        // Always manually set the read pointers.
        // pubsetbuf() is unreliable on libstdc++, and MSVC doesn't implement it.
        this->setg(data, data, data + size);
    }
};

class ofixedbuf : public std::streambuf {
public:
    ofixedbuf(char* data, std::size_t size) {
        // Always set buffer manually — pubsetbuf is useless on libstdc++
        this->setp(data, data + size);
    }

    std::size_t written() const {
        return this->pptr() - this->pbase();
    }
};

class iofixedbuf : public std::streambuf {
public:
    iofixedbuf(char* data, std::size_t size) {
        this->setg(data, data, data + size);
        this->setp(data, data + size);
    }
};



// Portable wall timer

// 1. Detect Standard and Platform
#if __cplusplus >= 201103L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201103L)
    #include <chrono>
    #define USE_CHRONO
#elif defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define USE_WINDOWS_QPC
#else
    #include <sys/time.h>
    #define USE_POSIX_GETTIMEOFDAY
#endif

class WallTimer {
public:
    // Define a wrapper for platform-specific time data
    struct TimeData {
#if defined(USE_CHRONO)
        std::chrono::steady_clock::time_point value;
#elif defined(USE_WINDOWS_QPC)
        LARGE_INTEGER value;
#elif defined(USE_POSIX_GETTIMEOFDAY)
        struct timeval value;
#endif

       // Converts the internal timestamp into a double representing total milliseconds
       // Note: On C++98 Windows, you need the frequency from the WallTimer class.
#if defined(USE_CHRONO)
       kanzi::uint64 to_ms() const {
           return static_cast<kanzi::uint64>(std::chrono::duration<double, std::milli>(value.time_since_epoch()).count());
       }
#elif defined(USE_WINDOWS_QPC)
       double to_ms(long long win_freq = 1) const {
           return static_cast<kanzi::uint64>(value.QuadPart) * 1000.0 / win_freq;
       }
#elif defined(USE_POSIX_GETTIMEOFDAY)
       double to_ms() const {
           return (static_cast<kanzi::uint64>(value.tv_sec) * 1000.0) + (value.tv_usec / 1000.0);
       }
#endif
    };

    WallTimer() {
#if defined(USE_WINDOWS_QPC)
        QueryPerformanceFrequency(&_frequency);
#endif
        _start = getCurrentTime();
    }

    // Method to get the current timestamp
    TimeData getCurrentTime() const {
        TimeData now;
#if defined(USE_CHRONO)
        now.value = std::chrono::steady_clock::now();
#elif defined(USE_WINDOWS_QPC)
        QueryPerformanceCounter(&now.value);
#elif defined(USE_POSIX_GETTIMEOFDAY)
        gettimeofday(&now.value, NULL);
#endif
        return now;
    }

    static double calculateDifference(const TimeData& start,
                                      const TimeData& end)
    {
#if defined(USE_CHRONO)

    return std::chrono::duration<double, std::milli>(
        end.value - start.value).count();

#elif defined(USE_WINDOWS_QPC)
        // C++98-compatible lazy initialization
        static LARGE_INTEGER frequency;
        static bool initialized = false;

        if (!initialized) {
            QueryPerformanceFrequency(&frequency);
            initialized = true;
        }

        return static_cast<double>(end.value.QuadPart - start.value.QuadPart)
               * 1000.0
               / static_cast<double>(frequency.QuadPart);
#elif defined(USE_POSIX_GETTIMEOFDAY)
        double sec  = end.value.tv_sec  - start.value.tv_sec;
        double usec = end.value.tv_usec - start.value.tv_usec;
        return (sec * 1000.0) + (usec / 1000.0);
#endif
    }

    // Convenience method for elapsed time since start
    double elapsed_ms() const {
        return calculateDifference(_start, getCurrentTime());
    }

private:
    TimeData _start;
#if defined(USE_WINDOWS_QPC)
    static LARGE_INTEGER _frequency;
    static bool _initialized;
#endif
};


#endif
