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
#ifndef knz_SliceArray
#define knz_SliceArray

#include <cstddef>
#include "ScratchAllocator.hpp"

namespace kanzi
{

   template <class T>
   class SliceArray
   {
   public:
      T* _array;
      int _length; // buffer length (a.k.a capacity)
      int _index;

      SliceArray(T* arr, int len, int index = 0) : _array(arr), _length(len), _index(index) {}

#if __cplusplus < 201103L
      SliceArray(const SliceArray& sa) { _array = sa._array; _length = sa._length; _index = sa._index; }

      SliceArray& operator=(const SliceArray& sa);

      ~SliceArray(){} // does not deallocate buffer memory
#else
      SliceArray(SliceArray&& sa) noexcept = default;

      SliceArray& operator=(SliceArray&& sa) noexcept = default;

      ~SliceArray() = default;
#endif

      // Utility methods
      static bool isValid(const SliceArray& sa);
      static void releaseBuffer(SliceArray& sa, ScratchAllocator* allocator = nullptr);
      static void ensureCapacity(SliceArray& sa, int requiredSize, ScratchAllocator* allocator = nullptr);
   };

   template <class T>
   inline bool SliceArray<T>::isValid(const SliceArray& sa) {
       return ((sa._array != nullptr) && (sa._index >= 0) && (sa._length >= 0) && (sa._index <= sa._length));
   }

   template <class T>
   inline void SliceArray<T>::releaseBuffer(SliceArray& sa, ScratchAllocator* allocator) {
       // Save and clear pointer/length before releasing so size-aware allocators
       // receive the prior capacity and exception unwinds never see stale pointers.
       T* arr = sa._array;
       const int len = sa._length;
       sa._array = nullptr;
       sa._length = 0;

       if (arr != nullptr) {
           if (allocator != nullptr)
               allocator->release(reinterpret_cast<byte*>(arr), std::size_t(len) * sizeof(T));
           else
               delete[] arr;
       }
   }

   template <class T>
   inline void SliceArray<T>::ensureCapacity(SliceArray& sa, int requiredSize, ScratchAllocator* allocator) {
       if ((sa._array != nullptr) && (sa._length >= requiredSize))
           return;

       releaseBuffer(sa, allocator);

       if (requiredSize > 0) {
           sa._array = (allocator != nullptr)
               ? reinterpret_cast<T*>(allocator->allocate(std::size_t(requiredSize) * sizeof(T)))
               : new T[requiredSize];
           sa._length = requiredSize;
       }
   }

#if __cplusplus < 201103L
   template <class T>
   inline SliceArray<T>& SliceArray<T>::operator=(const SliceArray& sa) {
      _array = sa._array;
      _length = sa._length;
      _index = sa._index;
      return *this;
   }
#endif

}
#endif

