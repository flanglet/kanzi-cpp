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

#ifndef _LZCodec_
#define _LZCodec_

#include "../Function.hpp"
#include <cstring>

namespace kanzi 
{

   // Simple byte oriented LZ77 implementation.
   // It is just LZ4 modified to use a bigger hash map.

   class LZCodec : public Function<byte>
   {
   public:
       LZCodec();

       ~LZCodec() { delete[] _buffer; _bufferSize = 0; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const 
       { 
           return (srcLen <= 1024) ? srcLen + 16 : srcLen + (srcLen / 64);
       }

   private:
      static const uint LZ_HASH_SEED      = 0x7FEB352D;
      static const uint HASH_LOG_SMALL    = 12;
      static const uint HASH_LOG_BIG      = 16;
      static const int MAX_DISTANCE       = (1 << 16) - 1;
      static const int SKIP_STRENGTH      = 6;
      static const int LAST_LITERALS      = 5;
      static const int MIN_MATCH          = 4;
      static const int MF_LIMIT           = 12;
      static const int ML_BITS            = 4;
      static const int ML_MASK            = (1 << ML_BITS) - 1;
      static const int RUN_BITS           = 8 - ML_BITS;
      static const int RUN_MASK           = (1 << RUN_BITS) - 1;
      static const int COPY_LENGTH        = 8;
      static const int MIN_LENGTH         = 14;
      static const int MAX_LENGTH         = (32*1024*1024) - 4 - MIN_MATCH;
      static const int SEARCH_MATCH_NB    = 1 << 6;

      int* _buffer;
      int _bufferSize;

      static int emitLength(byte block[], int len);

      static int emitLastLiterals(byte src[], byte dst[], int runLength);

      static bool differentInts(byte block[], int srcIdx, int dstIdx);

      static void customArrayCopy(byte src[], byte dst[], int len);
   };


   inline void LZCodec::customArrayCopy(byte src[], byte dst[], int len)
   {
       for (int i = 0; i < len; i += 8)
           memcpy(&dst[i], &src[i], 8);
   }

   inline bool LZCodec::differentInts(byte block[], int srcIdx, int dstIdx)
   {
       return *((int32*)&block[srcIdx]) != *((int32*)&block[dstIdx]);
   }

   inline int LZCodec::emitLength(byte block[], int length)
   {
       int idx = 0;

       while (length >= 0x1FE) {
           block[idx] = byte(0xFF);
           block[idx + 1] = byte(0xFF);
           idx += 2;
           length -= 0x1FE;
       }

       if (length >= 0xFF) {
           block[idx] = byte(0xFF);
           idx++;
           length -= 0xFF;
       }

       block[idx] = byte(length);
       return idx + 1;
   }
}
#endif