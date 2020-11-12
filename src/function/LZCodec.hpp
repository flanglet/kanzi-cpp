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

#include <cstring> // for memcpy
#include "../Context.hpp"
#include "../Function.hpp"
#include "../Memory.hpp"

namespace kanzi
{
	class LZCodec : public Function<byte> {
		friend class LZXCodec;
		friend class LZPCodec;

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

		Function<byte>* _delegate;
   };


   // Simple byte oriented LZ77 implementation.
   // It is a modified LZ4 with a bigger window, a bigger hash map, 3+n*8 bit
   // literal lengths and 17 or 24 bit match lengths.
   class LZXCodec : public Function<byte>
   {
   public:
       LZXCodec() { _hashes = new int32[0]; _hashSize = 0; _buffer = new byte[0]; _bufferSize = 0; }

       LZXCodec(Context&) { _hashes = new int32[0]; _hashSize = 0; _buffer = new byte[0]; _bufferSize = 0; }

       virtual ~LZXCodec() { delete[] _hashes; _hashSize = 0; delete[] _buffer; _bufferSize = 0; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return (srcLen <= 1024) ? srcLen + 16 : srcLen + (srcLen / 64);
       }

   private:
      static const uint HASH_SEED         = 0x1E35A7BD;
      static const uint HASH_LOG          = 19; // 512K
      static const uint HASH_SHIFT        = 40 - HASH_LOG;
      static const uint HASH_MASK         = (1 << HASH_LOG) - 1;
      static const int MAX_DISTANCE1      = (1 << 17) - 1;
      static const int MAX_DISTANCE2      = (1 << 24) - 1;
      static const int MIN_MATCH          = 5;
      static const int MAX_MATCH          = 32767 + MIN_MATCH;
      static const int MIN_BLOCK_LENGTH   = 24;
      static const int MIN_MATCH_MIN_DIST = 1 << 16;

      int32* _hashes;
      int _hashSize;
      byte* _buffer;
      int _bufferSize;

      static int emitLength(byte block[], int len);

      static int emitLastLiterals(const byte src[], byte dst[], int runLength);

      static void emitLiterals(const byte src[], byte dst[], int len);

      static int32 hash(const byte* p);
   };


   class LZPCodec : public Function<byte>
   {
   public:
       LZPCodec() { _hashes = new int32[0]; _hashSize = 0; }

       LZPCodec(Context&) { _hashes = new int32[0]; _hashSize = 0; }

       virtual ~LZPCodec() { delete[] _hashes; _hashSize = 0; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return (srcLen <= 1024) ? srcLen + 16 : srcLen + (srcLen / 64);
       }

   private:
      static const uint HASH_SEED         = 0x7FEB352D;
      static const uint HASH_LOG          = 16;
      static const uint HASH_SHIFT        = 32 - HASH_LOG;
      static const int MIN_MATCH          = 64;
      static const int MIN_BLOCK_LENGTH   = 128;
      static const int MATCH_FLAG         = 0xFC;

      int32* _hashes;
      int _hashSize;
  };



   inline bool LZCodec::sameInts(byte block[], int srcIdx, int dstIdx)
   {
       return *(reinterpret_cast<int32*>(&block[srcIdx])) == *(reinterpret_cast<int32*>(&block[dstIdx]));
   }


   inline void LZXCodec::emitLiterals(const byte src[], byte dst[], int len)
   {
       for (int i = 0; i < len; i += 8)
           memcpy(&dst[i], &src[i], 8);
   }

   inline int32 LZXCodec::hash(const byte* p)
   {
       return ((LittleEndian::readLong64(p) * HASH_SEED) >> HASH_SHIFT) & HASH_MASK;
   }

   inline int LZXCodec::emitLength(byte block[], int length)
   {
       int idx = 0;

       while (length >= 0xFF) {
           block[idx] = byte(0xFF);
           idx++;
           length -= 0xFF;
       }

       block[idx] = byte(length);
       return idx + 1;
   }
}
#endif