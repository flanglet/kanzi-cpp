/*
Copyright 2011-2021 Frederic Langlet
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
#ifndef _util_
#define _util_


#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include "types.hpp"

#ifdef CONCURRENCY_ENABLED
#include <mutex>
#endif

#ifdef _MSC_VER
   #define STAT _stat64
#else
   #if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
      #define STAT stat
   #else
      #define STAT stat64
   #endif
#endif


// Ahem ... Visual Studio
// This ostreambuf class is required because Microsoft cannot bother to implement
// streambuf::pubsetbuf().
template <typename T>
struct ostreambuf : public std::basic_streambuf<T, std::char_traits<T> >
{
    ostreambuf(T* buffer, std::streamsize length) {
       this->setp(buffer, &buffer[length]);
    }
};

template <typename T>
struct istreambuf : public std::basic_streambuf<T, std::char_traits<T> >
{
    istreambuf(T* buffer, std::streamsize length) {
       this->setg(buffer, buffer, &buffer[length]);
    }
};

template <typename T>
std::string to_string(T value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

inline std::string __trim(std::string& str, bool left, bool right)
{
    if (str.empty())
        return str;

    std::string::size_type begin = 0;
    std::string::size_type end = str.length() - 1;

    if (left) {
       while (begin <= end && (str[begin] <= 0x20 || str[begin] == 0x7F))
          begin++;
    }

    if (right) {
       while (end > begin && (str[end] <= 0x20 || str[end] == 0x7F))
          end--;
    }

    return str.substr(begin, end - begin + 1);
}

inline std::string trim(std::string& str)  { return __trim(str, true, true); }
inline std::string ltrim(std::string& str) { return __trim(str, true, false); }
inline std::string rtrim(std::string& str) { return __trim(str, false, true); }

inline bool samePaths(std::string& f1, std::string& f2)
{
   if (f1.compare(f2) == 0)
      return true;

   struct STAT buf1;
   int s1 = STAT(f1.c_str(), &buf1);
   struct STAT buf2;
   int s2 = STAT(f2.c_str(), &buf2);

   if (s1 != s2)
      return false;

   if (buf1.st_dev != buf2.st_dev)
      return false;

   if (buf1.st_ino != buf2.st_ino)
      return false;

   if (buf1.st_mode != buf2.st_mode)
      return false;

   if (buf1.st_nlink != buf2.st_nlink)
      return false;

   if (buf1.st_uid != buf2.st_uid)
      return false;

   if (buf1.st_gid != buf2.st_gid)
      return false;

   if (buf1.st_rdev != buf2.st_rdev)
      return false;

   if (buf1.st_size != buf2.st_size)
      return false;

   if (buf1.st_atime != buf2.st_atime)
      return false;

   if (buf1.st_mtime != buf2.st_mtime)
      return false;

   if (buf1.st_ctime != buf2.st_ctime)
      return false;

   return true;
}



#if __cplusplus >= 201103L || _MSC_VER >= 1700

#include <chrono>


class Clock {
private:
        std::chrono::steady_clock::time_point _start;
        std::chrono::steady_clock::time_point _stop;

public:
        Clock()
        {
                start();
                _stop = _start;
        }

        void start()
        {
                _start = std::chrono::steady_clock::now();
        }

        void stop()
        {
                _stop = std::chrono::steady_clock::now();
        }

        double elapsed() const
        {
                // In millisec
                return double(std::chrono::duration_cast<std::chrono::milliseconds>(_stop - _start).count());
        }
};
#else
#include <ctime>

class Clock {
private:
        clock_t _start;
        clock_t _stop;

public:
        Clock()
        {
            start();
            _stop = _start;
        }

        void start()
        {
           _start = clock();
        }

        void stop()
        {
           _stop = clock();
        }

        double elapsed() const
        {
           // In millisec
           return (_stop <= _start) ? 0.0 : double(_stop - _start) / CLOCKS_PER_SEC * 1000.0;
        }
};
#endif


//Prefetch
static inline void prefetchRead(const void* ptr) {
#if defined(__GNUG__) || defined(__clang__)
        __builtin_prefetch(ptr, 0, 1);
#elif defined(__x86_64__) || defined(_m_amd64)
        _mm_prefetch((char*) ptr, _MM_HINT_T0);
#elif defined(_M_ARM)
        __prefetch(ptr)
#elif defined(_M_ARM64)
        __prefetch2(ptr, 1)
#endif
}

static inline void prefetchWrite(const void* ptr) {
#if defined(__GNUG__) || defined(__clang__)
        __builtin_prefetch(ptr, 1, 1);
#elif defined(__x86_64__) || defined(_m_amd64)
        _mm_prefetch((char*) ptr, _MM_HINT_T0);
#elif defined(_M_ARM)
        __prefetchw(ptr)
#elif defined(_M_ARM64)
        __prefetch2(ptr, 17)
#endif
}


// Thread safe printer
class Printer
{
   public:
      Printer(std::ostream* os) { _os = os; }
      ~Printer() {}

      void println(const char* msg, bool print) {
         if ((print == true) && (msg != nullptr)) {
#ifdef CONCURRENCY_ENABLED
            std::lock_guard<std::mutex> lock(_mtx);
#endif
            (*_os) << msg << std::endl;
         }
      }

   private:
#ifdef CONCURRENCY_ENABLED
      static std::mutex _mtx;
#endif
      std::ostream* _os;
};

#endif
