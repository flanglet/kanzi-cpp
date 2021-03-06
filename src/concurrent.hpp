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

#ifndef _concurrent_
#define _concurrent_

#include "types.hpp"

template <class T>
class Task {
public:
    Task() {}
    virtual ~Task() {}
    virtual T run() = 0;
};

#if __cplusplus >= 201103L || _MSC_VER >= 1700
	// C++ 11 (or partial)
	#include <atomic>

	#ifndef CONCURRENCY_ENABLED
		#ifdef __clang__
			// Process clang first because it may define __GNUC__ with an old version
			#define CONCURRENCY_ENABLED
		#elif __GNUC__
			// Require g++ 5.0 minimum, 4.8.4 generates exceptions on futures (?)
			#if ((__GNUC__ << 16) + __GNUC_MINOR__ >= (5 << 16) + 0)
				#define CONCURRENCY_ENABLED
			#endif
		#else
			#define CONCURRENCY_ENABLED
		#endif
	#endif
#endif




#ifdef __x86_64__
   #ifdef __clang__
	   #define CPU_PAUSE() __builtin_ia32_pause()
   #elif __GNUC__
	   #define CPU_PAUSE() __builtin_ia32_pause()
   #elif _MSC_VER
	   #define CPU_PAUSE() _mm_pause()
   #else
      #define CPU_PAUSE()
   #endif
#else
   #define CPU_PAUSE()
#endif



#ifdef CONCURRENCY_ENABLED

	template<class T>
	class BoundedConcurrentQueue {
	public:
		BoundedConcurrentQueue(int nbItems, T* data) { _index = 0; _data = data; _size = nbItems; }

		~BoundedConcurrentQueue() { }

		T* get() { int idx = _index.fetch_add(1); return (idx >= _size) ? nullptr : &_data[idx]; }

		void clear() { _index.store(_size); }

	private:
		std::atomic_int _index;
		int _size;
		T* _data;
	};

#elif (__cplusplus && __cplusplus < 201103L) || (_MSC_VER && _MSC_VER < 1700)
	// ! Stubs for NON CONCURRENT USAGE !
	// Used to compile and provide a non concurrent version AND
	// when atomic.h is not available (VS C++)
	const int memory_order_relaxed = 0;
	const int memory_order_acquire = 2;
	const int memory_order_release = 3;
	#include <iostream>

	class atomic_int {
	private:
		int _n;

	public:
		atomic_int(int n=0) { _n = n; }
		atomic_int& operator=(int n) {
			_n = n;
			return *this;
		}
		int load(int mo = memory_order_relaxed) const { return _n; }
		void store(int n, int mo = memory_order_release) { _n = n; }
		atomic_int& operator++(int) {
			_n++;
			return *this;
		}
		atomic_int fetch_add(atomic_int) {
		   _n++;
		   return atomic_int(_n-1);
		}
		bool compare_exchange_strong(int& expected, int desired) {
		   if (_n == expected) {
			   _n = desired;
			   return true;
		   }

		   return false;
		}
	};

	class atomic_bool {
	private:
		bool _b;

	public:
		atomic_bool(bool b=false) { _b = b; }
		atomic_bool& operator=(bool b) {
			_b = b;
			return *this;
		}
		bool load(int mo = memory_order_relaxed) const { return _b; }
		void store(bool b, int mo = memory_order_release) { _b = b; }
		bool exchange(bool expected, int mo = memory_order_acquire) {
			bool b = _b;
			_b = expected;
			return b;
		}
	};
#endif //   (__cplusplus && __cplusplus < 201103L) || (_MSC_VER && _MSC_VER < 1700)



#endif
