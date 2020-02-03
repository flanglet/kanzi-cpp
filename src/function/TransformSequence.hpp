/*
Copyright 2011-2017 Frederic Langlet
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

#ifndef _TransformSequence_
#define _TransformSequence_

#include "../Function.hpp"

using namespace std;

namespace kanzi 
{

   // Encapsulates a sequence of transforms or functions in a function
   template <class T>
   class TransformSequence : public Function<T> {
   public:
       TransformSequence(Transform<T>* transforms[8], bool deallocate = true) THROW;

       ~TransformSequence();

       bool forward(SliceArray<T>& input, SliceArray<T>& output, int length);

       bool inverse(SliceArray<T>& input, SliceArray<T>& output, int length);

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const;

       byte getSkipFlags() const { return _skipFlags; }

       void setSkipFlags(byte flags) { _skipFlags = flags; }

       int getNbFunctions() const { return _length; }

   private:
       static const byte SKIP_MASK = byte(0xFF);

       Transform<T>* _transforms[8]; // transforms or functions
       bool _deallocate; // deallocate memory for transforms ?
       int _length; // number of transforms
       byte _skipFlags; // skip transforms
   };

   template <class T>
   TransformSequence<T>::TransformSequence(Transform<T>* transforms[8], bool deallocate) THROW
   {
       _deallocate = deallocate;
       _length = 8;
       _skipFlags = byte(0);

       for (int i = 7; i >= 0; i--) {
           _transforms[i] = transforms[i];

           if (_transforms[i] == nullptr)
               _length = i;
       }

       if (_length == 0)
           throw invalid_argument("At least one transform required");
   }

   template <class T>
   TransformSequence<T>::~TransformSequence()
   {
       if (_deallocate == true) {
           for (int i = 0; i < 8; i++) {
               if (_transforms[i] != nullptr)
                   delete _transforms[i];
           }
       }
   }

   template <class T>
   bool TransformSequence<T>::forward(SliceArray<T>& input, SliceArray<T>& output, int count) THROW
   {
       if (!SliceArray<byte>::isValid(input))
           throw invalid_argument("Invalid input block");

       if (!SliceArray<byte>::isValid(output))
           throw invalid_argument("Invalid output block");

       if ((count < 0) || (count + input._index > input._length))
           return false;

       _skipFlags = byte(SKIP_MASK);

       if (count == 0)
           return true;

       SliceArray<T>* in = &input;
       SliceArray<T>* out = &output;
       const int blockSize = count;
       const int requiredSize = getMaxEncodedLength(blockSize);
       int swaps = 0;

       // Process transforms sequentially
       for (int i = 0; i < _length; i++) {
            // Check that the output buffer has enough room. If not, allocate a new one.
            if (out->_length < requiredSize) {
                delete[] out->_array;
                out->_array = new byte[requiredSize];
                out->_length = requiredSize; 
            }

            const int savedIIdx = in->_index;
            const int savedOIdx = out->_index;

            // Apply forward transform
            if (_transforms[i]->forward(*in, *out, count) == false) {
                // Transform failed. Either it does not apply to this type
                // of data or a recoverable error occured => revert
                in->_index = savedIIdx;
                out->_index = savedOIdx;
                continue;
            }

            _skipFlags &= ~byte(1 << (7 - i));
            count = out->_index - savedOIdx;
            in->_index = savedIIdx;
            out->_index = savedOIdx;
            swap(in, out);
            swaps++;
       }

       if ((swaps & 1) == 0)
           memcpy(&output._array[output._index], &in->_array[in->_index], count);
     
       input._index += blockSize;
       output._index += count;
       return _skipFlags != SKIP_MASK;
   }

   template <class T>
   bool TransformSequence<T>::inverse(SliceArray<T>& input, SliceArray<T>& output, int count) THROW
   {
       if (!SliceArray<byte>::isValid(input))
           throw invalid_argument("Invalid input block");

       if (!SliceArray<byte>::isValid(output))
           throw invalid_argument("Invalid output block");

       if ((count < 0) || (count + input._index > input._length))
           return false;

       if (count == 0)
           return true;

       if (_skipFlags == SKIP_MASK) {
           memcpy(&output._array[output._index], &input._array[input._index], count);
           input._index += count;
           output._index += count;
           return true;
       }

       const int blockSize = count;
       bool res = true;
       SliceArray<T>* in = &input;
       SliceArray<T>* out = &output;
       int swaps = 0;

       // Process transforms sequentially in reverse order
       for (int i = _length - 1; i >= 0; i--) {
           if ((_skipFlags & byte(1 << (7 - i))) != byte(0))
               continue;

           // Check that the output buffer has enough room. If not, allocate a new one.
           if (out->_length < output._length) {
              delete[] out->_array;
              out->_array = new byte[output._length];
              out->_length = output._length;
           }

           const int savedIIdx = in->_index;
           const int savedOIdx = out->_index;

           // Apply inverse transform
           res = _transforms[i]->inverse(*in, *out, count);

           // All inverse transforms must succeed
           if (res == false)
               break;

           count = out->_index - savedOIdx;
           in->_index = savedIIdx;
           out->_index = savedOIdx;
           swap(in, out);
           swaps++;
       }

       if ((res == true) && ((swaps & 1) == 0))
           memcpy(&output._array[output._index], &input._array[input._index], count);

       input._index += blockSize;
       output._index += count;
       return res;
   }

   template <class T>
   int TransformSequence<T>::getMaxEncodedLength(int srcLength) const
   {
       int requiredSize = srcLength;

       for (int i = 0; i < _length; i++) {
           Function<T>* f = dynamic_cast<Function<T>*>(_transforms[i]);

           if (f != nullptr) {
               const int reqSize = f->getMaxEncodedLength(requiredSize);

               if (reqSize > requiredSize)
                   requiredSize = reqSize;
           }
       }

       return requiredSize;
   }
}
#endif
