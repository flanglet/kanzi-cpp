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

#ifndef _SliceArray_
#define _SliceArray_

namespace kanzi
{

   template <class T>
   class SliceArray
   {
   public:
      T* _array;
      int _length; // buffer length (a.k.a capacity)
      int _index;

      SliceArray(T* arr, int len, int index = 0) { _array = arr; _length = len; _index = index; }

#if __cplusplus < 201103L
      SliceArray(const SliceArray& sa) { _array = sa._array; _length = sa._length; _index = sa._index; }

      SliceArray& operator=(const SliceArray& sa);

      ~SliceArray(){} // does not deallocate buffer memory
#else
      SliceArray(SliceArray&& sa) = default;

      SliceArray& operator=(SliceArray&& sa) = default;

      ~SliceArray() = default;
#endif

      // Utility methods
      static bool isValid(const SliceArray& sa);

      SliceArray& realloc(int newLength, bool keepData = true);
   };

   template <class T>
   inline bool SliceArray<T>::isValid(const SliceArray& sa) {
       if (sa._array == nullptr)
          return false;

       if (sa._index < 0)
          return false;

       if (sa._length < 0)
          return false;

       return (sa._index <= sa._length);
   }

   template <class T>
   inline SliceArray<T>& SliceArray<T>::realloc(int newLength, bool keepData) {
       if ((newLength >= 0) && (newLength <= _length)) {
          _length = newLength;
          return *this;
       }

       T* arr = new T[newLength];

       if (_array != nullptr) {
          if ((keepData == true) && (_length != 0))
             memcpy(&arr[0], &_array[0], _length);

          delete[] _array;
       }

       _array = arr;
       _length = newLength;
       return *this;
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