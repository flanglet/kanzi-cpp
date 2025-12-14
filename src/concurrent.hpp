/*
Copyright 2011-2025 Frederic Langlet
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
#ifndef _concurrent_
#define _concurrent_

#include "types.hpp"

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1700)
    // C++ 11 (or partial)
    #include <atomic>

    #ifndef CONCURRENCY_DISABLED
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


#ifdef CONCURRENCY_ENABLED
   #include <vector>
   #include <queue>
   #include <memory>
   #include <thread>
   #include <mutex>
   #include <condition_variable>
   #include <future>
   #include <functional>
   #include <stdexcept>

   #ifdef __x86_64__
      #ifdef __clang__
          #define CPU_PAUSE() __builtin_ia32_pause()
      #elif __GNUC__
          #define CPU_PAUSE() __builtin_ia32_pause()
      #elif _MSC_VER
          #include <immintrin.h>
          #define CPU_PAUSE() _mm_pause()
      #else
         #define CPU_PAUSE() std::this_thread::yield();
      #endif
   #else
      #define CPU_PAUSE() std::this_thread::yield();
   #endif
#else
   #define CPU_PAUSE()
#endif


template <class T>
class Task {
    public:
        Task() {}
        virtual ~Task() {}
        virtual T run() = 0;
};


#ifdef CONCURRENCY_ENABLED
   class ThreadPool FINAL {
   public:
       ThreadPool(int threads = 8);

       template<class F, class... Args>
#if __cplusplus >= 201703L // result_of deprecated from C++17
       std::future<typename std::invoke_result_t<F, Args...>> schedule(F&& f, Args&&... args);
#else
       std::future<typename std::result_of<F(Args...)>::type> schedule(F&& f, Args&&... args);
#endif

       ~ThreadPool() noexcept;

   private:
       std::vector<std::thread> _workers;
       std::queue<std::function<void()>> _tasks;
       std::mutex _mutex;
       std::condition_variable _condition;
       bool _stop;
   };


   inline ThreadPool::ThreadPool(int threads)
       :   _stop(false)
   {
       if ((threads <= 0) || (threads > 1024))
           throw std::invalid_argument("The number of threads must be in [1..1024]");

       // Start and run threads
       for (int i = 0; i < threads; i++)
           _workers.emplace_back(
               [this]
               {
                   for(;;)
                   {
                       std::function<void()> task;

                       {
                           std::unique_lock<std::mutex> lock(_mutex);
                           _condition.wait(lock,
                               [this] { return _stop || !_tasks.empty(); });

                           if (_stop && _tasks.empty())
                               return;

                           task = std::move(_tasks.front());
                           _tasks.pop();
                       }

                       task();
                   }
               }
           );
   }


   template<class F, class... Args>
#if __cplusplus >= 201703L // result_of deprecated from C++17
   std::future<typename std::invoke_result_t<F, Args...> > ThreadPool::schedule(F&& f, Args&&... args)
   {
       using return_type = typename std::invoke_result<F, Args...>::type;
#else
   std::future<typename std::result_of<F(Args...)>::type> ThreadPool::schedule(F&& f, Args&&... args)
   {
       using return_type = typename std::result_of<F(Args...)>::type;
#endif

       auto task = std::make_shared< std::packaged_task<return_type()> >(
               std::bind(std::forward<F>(f), std::forward<Args>(args)...)
           );

       std::future<return_type> res = task->get_future();

       {
           std::unique_lock<std::mutex> lock(_mutex);

           if (_stop == true)
               throw std::runtime_error("ThreadPool stopped");

           _tasks.emplace([task](){ (*task)(); });
       }

       _condition.notify_one();
       return res;
   }


   // the destructor joins all threads
   inline ThreadPool::~ThreadPool() noexcept
   {
       {
           std::unique_lock<std::mutex> lock(_mutex);
           _stop = true;
       }

       _condition.notify_all();

       for (std::thread& w : _workers)
           w.join();
   }



    template<class T>
    class BoundedConcurrentQueue {
    public:
        BoundedConcurrentQueue(int nbItems, T* data) : _index(0), _size(nbItems), _data(data) {}

        ~BoundedConcurrentQueue() { }

        T* get() { int idx = _index.fetch_add(1, std::memory_order_acq_rel); return (idx >= _size) ? nullptr : &_data[idx]; }

        void clear() { _index.store(_size); }

    private:
        std::atomic_int _index;
        int _size;
        T* _data;
    };

#endif


#if defined(CONCURRENCY_ENABLED) || (__cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1700)
   #include <atomic>
   #define ATOMIC_INT std::atomic_int
   #define ATOMIC_BOOL std::atomic_bool

#else
   // ! Stubs for NON CONCURRENT USAGE !
   // Used when compiling for older C++ standards (C++98/03)

   // Use enum instead of const int to prevent linkage issues
   enum fallback_memory_order {
       memory_order_relaxed = 0,
       memory_order_consume = 1,
       memory_order_acquire = 2,
       memory_order_release = 3,
       memory_order_acq_rel = 4,
       memory_order_seq_cst = 5
   };

   // Naming the class 'fallback_...' avoids conflicts if the macro
   // matches the class name recursively in some preprocessors.
   class fallback_atomic_int {
   private:
       volatile int _n;

       // Disable copy constructor and assignment operator
       // (Atomics should not be copyable)
       fallback_atomic_int(const fallback_atomic_int&);
       fallback_atomic_int& operator=(const fallback_atomic_int&);

   public:
       fallback_atomic_int(int n = 0) : _n(n) {}

       // Assignment returns the value (int), NOT the object reference
       int operator=(int n)
       {
           _n = n;
           return n;
       }

       operator int() const
       {
           return _n;
       }

       int load(int mo = memory_order_relaxed) const
       {
           (void)mo;
           return _n;
       }

       void store(int n, int mo = memory_order_release)
       {
           (void)mo;
           _n = n;
       }

       // Postfix ++ (x++) returns OLD value
       int operator++(int)
       {
           int old = _n;
           _n++;
           return old;
       }

       // Prefix ++ (++x) returns NEW value
       int operator++()
       {
           return ++_n;
       }

       // Standard signature: takes int delta, returns OLD value
       int fetch_add(int delta, int mo = memory_order_seq_cst)
       {
          (void)mo;
          int old = _n;
          _n += delta;
          return old;
       }

       int fetch_sub(int delta, int mo = memory_order_seq_cst)
       {
          (void)mo;
          int old = _n;
          _n -= delta;
          return old;
       }

       // CRITICAL FIX: Must update 'expected' on failure
       bool compare_exchange_strong(int& expected, int desired, int mo = memory_order_seq_cst)
       {
           (void)mo;

           if (_n == expected) {
               _n = desired;
               return true;
           } else {
               expected = _n;
               return false;
           }
       }

       bool compare_exchange_weak(int& expected, int desired, int mo = memory_order_seq_cst)
       {
           return compare_exchange_strong(expected, desired, mo);
       }
   };

   class fallback_atomic_bool {
   private:
       volatile bool _b;

       fallback_atomic_bool(const fallback_atomic_bool&);
       fallback_atomic_bool& operator=(const fallback_atomic_bool&);

   public:
       fallback_atomic_bool(bool b = false) : _b(b) {}

       bool operator=(bool b)
       {
           _b = b;
           return b;
       }

       operator bool() const
       {
           return _b;
       }

       bool load(int mo = memory_order_relaxed) const
       {
           (void)mo;
           return _b;
       }

       void store(bool b, int mo = memory_order_release)
       {
           (void)mo;
           _b = b;
       }

       bool exchange(bool val, int mo = memory_order_acquire)
       {
           (void)mo;
           bool old = _b;
           _b = val;
           return old;
       }

       bool compare_exchange_strong(bool& expected, bool desired, int mo = memory_order_seq_cst)
       {
           (void)mo;
           if (_b == expected) {
               _b = desired;
               return true;
           } else {
               expected = _b;
               return false;
           }
       }
   };

   #define ATOMIC_INT fallback_atomic_int
   #define ATOMIC_BOOL fallback_atomic_bool

#endif


#endif

