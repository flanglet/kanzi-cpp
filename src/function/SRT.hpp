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

#ifndef _SRT_
#define _SRT_

#include "../Function.hpp"

namespace kanzi {
   // Behemoth Rank Transform
   // A strong rank transform based on https://github.com/loxxous/Behemoth-Rank-Coding
   // by Lucas Marsh. Typically used post BWT to reduce the variance of the data 
   // prior to entropy coding.

   class SRT : public Function<byte> {
   public:
       SRT() {}

       ~SRT() {}

       bool forward(SliceArray<byte>& pSrc, SliceArray<byte>& pDst, int length) THROW;

       bool inverse(SliceArray<byte>& pSrc, SliceArray<byte>& pDst, int length) THROW;

       int getMaxEncodedLength(int srcLen) const { return srcLen + HEADER_SIZE; }

   private:
       static const int HEADER_SIZE = 4 * 256; // freqs

       static int preprocess(int32 freqs[], byte symbols[]);

       static int encodeHeader(int32 freqs[], byte dst[]);

       static int decodeHeader(byte src[], int32 freqs[]);
   };
}
#endif
