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

#ifndef _types_
#define _types_

#include <cstddef>
#include <stdlib.h>

	#ifdef _MSC_VER
		#if !defined(__x86_64__)
			#define __x86_64__  _M_X64
		#endif
		#if !defined(__i386__)
			#define __i386__  _M_IX86
		#endif
	#endif


	#ifdef _MSC_VER
		#include <intrin.h>
		#define popcount __popcnt
	#else
		#ifdef  __INTEL_COMPILER
			#include <intrin.h>
			#define popcount _popcnt32
		#else
			#define popcount __builtin_popcount
		#endif
	#endif


	#ifdef __x86_64__
	   #include <emmintrin.h>
	#endif

   /*
   Visual Studio 2019 Update 4   MSVC++ 14.24 _MSC_VER == 1924
   Visual Studio 2019 Update 3   MSVC++ 14.21 _MSC_VER == 1923
   Visual Studio 2019 Update 2   MSVC++ 14.21 _MSC_VER == 1922
   Visual Studio 2019 Update 1   MSVC++ 14.21 _MSC_VER == 1921
   Visual Studio 2019            MSVC++ 14.20 _MSC_VER == 1920
   Visual Studio 2017 Update 9   MSVC++ 14.16 _MSC_VER == 1916
   Visual Studio 2017 Update 8   MSVC++ 14.15 _MSC_VER == 1915
   Visual Studio 2017 Update 7   MSVC++ 14.14 _MSC_VER == 1914
   Visual Studio 2017 Update 6   MSVC++ 14.13 _MSC_VER == 1913
   Visual Studio 2017 Update 5   MSVC++ 14.12 _MSC_VER == 1912
   Visual Studio 2017 Update 3&4 MSVC++ 14.11 _MSC_VER == 1911
   Visual Studio 2017            MSVC++ 14.10 _MSC_VER == 1910
   Visual Studio 2015            MSVC++ 14    _MSC_VER == 1900
   Visual Studio 2013            MSVC++ 12    _MSC_VER == 1800
   Visual Studio 2012            MSVC++ 11    _MSC_VER == 1700
   Visual Studio 2010            MSVC++ 10    _MSC_VER == 1600
   Visual Studio 2008            MSVC++ 9     _MSC_VER == 1500
   Visual Studio 2005            MSVC++ 8     _MSC_VER == 1400
   Visual Studio 2003 Beta       MSVC++ 7.1   _MSC_VER == 1310
   Visual Studio 2002            MSVC++ 7     _MSC_VER == 1300
   Visual Studio                 MSVC++ 6.0   _MSC_VER == 1200
   Visual Studio                 MSVC++ 5     _MSC_VER == 1100
   Visual Studio                 MSVC++ 4.2   _MSC_VER == 1020
   Visual Studio                 MSVC++ 4.1   _MSC_VER == 1010
   Visual Studio                 MSVC++ 4     _MSC_VER == 1000
   Visual Studio                 MSVC++ 2     _MSC_VER == 900
   Visual Studio                 MSVC++ 1     _MSC_VER == 800
   */

   #ifdef _MSC_VER
      #if _MSC_VER == 1300
         #define _MSC_VER_STR 2003
      #endif
      #if _MSC_VER == 1400
         #define _MSC_VER_STR 2005
      #endif
      #if _MSC_VER == 1500
         #define _MSC_VER_STR 2008
      #endif
      #if _MSC_VER == 1600
         #define _MSC_VER_STR 2010
      #endif
      #if _MSC_VER == 1700
         #define _MSC_VER_STR 2012
      #endif
      #if _MSC_VER == 1800
         #define _MSC_VER_STR 2013
      #endif
      #if _MSC_VER == 1900
         #define _MSC_VER_STR 2015
      #endif
      #if _MSC_VER >= 1910 && _MSC_VER <= 1916
         #define _MSC_VER_STR 2017
      #endif
      #if _MSC_VER >= 1920
         #define _MSC_VER_STR 2019
      #endif
   #endif

	#ifndef _GLIBCXX_USE_NOEXCEPT
	#define _GLIBCXX_USE_NOEXCEPT
	//#define _GLIBCXX_USE_NOEXCEPT throw()
	#endif

	#ifndef THROW
	   #if __cplusplus >= 201103L
	      #define THROW
	   #else
          #if defined(__GNUC__)
		     #define THROW
          #else
	         #define THROW throw(...)
          #endif
	   #endif
	#endif

	#if __cplusplus >= 201103L
	   // C++ 11
	   #include <cstdint>
	#else
	   #if defined(_MSC_VER) && _MSC_VER < 1300
	      typedef signed char int8_t;
	      typedef signed short int16_t;
	      typedef signed int int32_t;
	      typedef unsigned char uint8_t;
	      typedef unsigned short uint16_t;
	      typedef unsigned int uint32_t;
	   #else
	      typedef signed __int8 int8_t;
	      typedef signed __int16 int16_t;
	      typedef signed __int32 int32_t;
	      typedef unsigned __int8 uint8_t;
	      typedef unsigned __int16 uint16_t;
	      typedef unsigned __int32 uint32_t;
	   #endif

	   typedef signed __int64 int64_t;
	   typedef unsigned __int64 uint64_t;
	   #define nullptr NULL
	#endif

#if __cplusplus >= 201703L
	// byte is defined in C++17 and above
	#include <cstddef>
	typedef std::byte byte;
#else
	typedef uint8_t byte;
#endif

	typedef int8_t int8;
	typedef uint8_t uint8;
	typedef int16_t int16;
	typedef int32_t int32;
	typedef int64_t int64;
	typedef uint16_t uint16;
	typedef uint32_t uint;
	typedef uint32_t uint32;
	typedef uint64_t uint64;


   #if defined(WIN32) || defined(_WIN32)
      #define PATH_SEPARATOR '\\'
   #else
      #define PATH_SEPARATOR '/'
   #endif


   #if defined(_MSC_VER)
      #define ALIGNED_(x) __declspec(align(x))
   #else
      #if defined(__GNUC__)
         #define ALIGNED_(x) __attribute__ ((aligned(x)))
      #endif
   #endif


   #ifndef STR_TRUE
      #define STR_TRUE "1"
   #endif

   #ifndef STR_FALSE
      #define STR_FALSE "0"
   #endif

#endif
