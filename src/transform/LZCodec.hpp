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

#ifndef _LZCodec_
#define _LZCodec_

#include <cstring> // for memcpy
#include "../Context.hpp"
#include "../Transform.hpp"
#include "../Memory.hpp"

namespace kanzi {
   class LZCodec : public Transform<byte> {

   public:
       LZCodec() THROW;

       LZCodec(Context& ctx) THROW;

       virtual ~LZCodec() { delete _delegate; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       static bool sameInts(byte block[], int srcIdx, int dstIdx);

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return _delegate->getMaxEncodedLength(srcLen);
       }

   private:
       Transform<byte>* _delegate;
   };

   // Simple byte oriented LZ77 implementation.
   // It is a based on a heavily modified LZ4 with a bigger window, a bigger
   // hash map, 3+n*8 bit literal lengths and 17 or 24 bit match lengths.
   template <bool T>
   class LZXCodec : public Transform<byte> {
   public:
       LZXCodec()
       {
           _hashes = new int32[0];
           _hashSize = 0;
           _tkBuf = new byte[0];
           _mBuf = new byte[0];
           _bufferSize = 0;
       }

       LZXCodec(Context&)
       {
           _hashes = new int32[0];
           _hashSize = 0;
           _tkBuf = new byte[0];
           _mBuf = new byte[0];
           _bufferSize = 0;
       }

       virtual ~LZXCodec()
       {
           delete[] _hashes;
           _hashSize = 0;
           delete[] _mBuf;
           _bufferSize = 0;
           delete[] _tkBuf;
       }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return (srcLen <= 1024) ? srcLen + 16 : srcLen + (srcLen / 64);
       }

   private:
       static const uint HASH_SEED = 0x1E35A7BD;
       static const uint HASH_LOG1 = 16;
       static const uint HASH_SHIFT1 = 40 - HASH_LOG1;
       static const uint HASH_MASK1 = (1 << HASH_LOG1) - 1;
       static const uint HASH_LOG2 = 21;
       static const uint HASH_SHIFT2 = 48 - HASH_LOG2;
       static const uint HASH_MASK2 = (1 << HASH_LOG2) - 1;
       static const int MAX_DISTANCE1 = (1 << 17) - 2;
       static const int MAX_DISTANCE2 = (1 << 24) - 2;
       static const int MIN_MATCH = 5;
       static const int MAX_MATCH = 65535 + 254 + 15 + MIN_MATCH;
       static const int MIN_BLOCK_LENGTH = 24;
       static const int MIN_MATCH_MIN_DIST = 1 << 16;

       int32* _hashes;
       int _hashSize;
       byte* _mBuf;
       byte* _tkBuf;
       int _bufferSize;

       static int emitLength(byte block[], int len);

       static void emitLiterals(const byte src[], byte dst[], int len);

       static int readLength(byte block[], int& pos);

       static int32 hash(const byte* p);

       static int findMatch(byte block[], int pos, int ref, int maxMatch);
   };

   class LZPCodec : public Transform<byte> {
   public:
       LZPCodec()
       {
           _hashes = new int32[0];
           _hashSize = 0;
       }

       LZPCodec(Context&)
       {
           _hashes = new int32[0];
           _hashSize = 0;
       }

       virtual ~LZPCodec()
       {
           delete[] _hashes;
           _hashSize = 0;
       }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return (srcLen <= 1024) ? srcLen + 16 : srcLen + (srcLen / 64);
       }

   private:
       static const uint HASH_SEED = 0x7FEB352D;
       static const uint HASH_LOG = 16;
       static const uint HASH_SHIFT = 32 - HASH_LOG;
       static const int MIN_MATCH = 96;
       static const int MIN_BLOCK_LENGTH = 128;
       static const int MATCH_FLAG = 0xFC;

       int32* _hashes;
       int _hashSize;
   };

   inline bool LZCodec::sameInts(byte block[], int srcIdx, int dstIdx)
   {
       return memcmp(&block[srcIdx], &block[dstIdx], 4) == 0;
   }

   template <bool T>
   inline void LZXCodec<T>::emitLiterals(const byte src[], byte dst[], int len)
   {
       for (int i = 0; i < len; i += 8)
           memcpy(&dst[i], &src[i], 8);
   }

   template <bool T>
   inline int32 LZXCodec<T>::hash(const byte* p)
   {
       if (T == true) {
           return ((LittleEndian::readLong64(p) * HASH_SEED) >> HASH_SHIFT2) & HASH_MASK2;
       } else {
           return ((LittleEndian::readLong64(p) * HASH_SEED) >> HASH_SHIFT1) & HASH_MASK1;
       }
   }

   template <bool T>
   inline int LZXCodec<T>::emitLength(byte block[], int length)
   {
       if (length < 254) {
           block[0] = byte(length);
           return 1;
       }

       if (length < 65536 + 254) {
           length -= 254;
           block[0] = byte(254);
           block[1] = byte(length >> 8);
           block[2] = byte(length);
           return 3;
       }

       length -= 255;
       block[0] = byte(255);
       block[1] = byte(length >> 16);
       block[2] = byte(length >> 8);
       block[3] = byte(length);
       return 4;
   }

   template <bool T>
   inline int LZXCodec<T>::readLength(byte block[], int& pos)
   {
       int res = int(block[pos++]);

       if (res < 254)
           return res;

       if (res == 254) {
           res += (int(block[pos++]) << 8);
           res += int(block[pos++]);
           return res;
       }

       res += (int(block[pos]) << 16);
       res += (int(block[pos + 1]) << 8);
       res += int(block[pos + 2]);
       pos += 3;
       return res;
   }

   template <bool T>
   inline int LZXCodec<T>::findMatch(byte src[], int srcIdx, int ref, int maxMatch)
   {
       if (LZCodec::sameInts(src, ref, srcIdx) == false)
           return 0;

       int bestLen =  4;

       while ((bestLen + 4 < maxMatch) && (LZCodec::sameInts(src, ref + bestLen, srcIdx + bestLen) == true))
           bestLen += 4;

       while ((bestLen < maxMatch) && (src[ref + bestLen] == src[srcIdx + bestLen]))
           bestLen++;

       return bestLen;
   }
}
#endif