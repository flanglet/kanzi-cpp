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

#ifndef _BRT_
#define _BRT_

#include "../Function.hpp"

namespace kanzi {
   // Behemoth Rank Transform
   // A strong rank transform based on https://github.com/loxxous/Behemoth-Rank-Coding
   // by Lucas Marsh. Typically used post BWT to reduce the variance of the data 
   // prior to entropy coding.

   class BRT : public Function<byte> {
   public:
       BRT() {}

       ~BRT() {}

       bool forward(SliceArray<byte>& pSrc, SliceArray<byte>& pDst, int length) THROW;

       bool inverse(SliceArray<byte>& pSrc, SliceArray<byte>& pDst, int length) THROW;

       int getMaxEncodedLength(int srcLen) const { return srcLen + HEADER_SIZE; }

   private:
       static const int HEADER_SIZE = 1024 + 1; // 4*256 freqs + 1 nbSymbols

       static void sortMap(int32 freqs[], uint8 map[]);

       static int computeFrequencies(SliceArray<byte>& input, int length, int32 freqs[], int32 ranks[]);

       static int encodeHeader(byte block[], int32 freqs[], int nbSymbols);

       static int decodeHeader(byte block[], int32 freqs[], int& nbSymbols, int& total);
   };
}
#endif
