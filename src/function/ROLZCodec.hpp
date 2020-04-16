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

#ifndef _ROLZCodec_
#define _ROLZCodec_

#include "../Context.hpp"
#include "../Function.hpp"
#include "../Memory.hpp"
#include "../Predictor.hpp"
#include "../util.hpp"


// Implementation of a Reduced Offset Lempel Ziv transform
// More information about ROLZ at http://ezcodesample.com/rolz/rolz_article.html

namespace kanzi {

   class ROLZEncoder {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;
       static const int MATCH_FLAG = 0;
       static const int LITERAL_FLAG = 1;
       static const int PSCALE = 0xFFFF;

       uint16* _probs[2];
       uint _logSizes[2];
       int& _idx;
       uint64 _low;
       uint64 _high;
       byte* _buf;
       int32 _c1;
       int32 _ctx;
       int _pIdx;

       void encodeBit(int bit);

   public:
       ROLZEncoder(uint litLogSize, uint mLogSize, byte buf[], int& idx);

       ~ROLZEncoder()
       {
           delete[] _probs[LITERAL_FLAG];
           delete[] _probs[MATCH_FLAG];
       }

       void encodeBits(int val, int n);

       void dispose();

       void reset();

       void setMode(int n) { _pIdx = n; }

       void setContext(byte ctx) { _ctx = int32(ctx) << _logSizes[_pIdx]; }
   };

   class ROLZDecoder {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_56 = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;
       static const int MATCH_FLAG = 0;
       static const int LITERAL_FLAG = 1;
       static const int PSCALE = 0xFFFF;

       uint16* _probs[2];
       uint _logSizes[2];
       int& _idx;
       uint64 _low;
       uint64 _high;
       uint64 _current;
       byte* _buf;
       int32 _c1;
       int32 _ctx;
       int _pIdx;

       int decodeBit();

   public:
       ROLZDecoder(uint litLogSize, uint mLogSize, byte buf[], int& idx);

       ~ROLZDecoder()
       {
           delete[] _probs[LITERAL_FLAG];
           delete[] _probs[MATCH_FLAG];
       }

       int decodeBits(int n);

       void dispose() {}

       void reset();

       void setMode(int n) { _pIdx = n; }

       void setContext(byte ctx) { _ctx = int32(ctx) << _logSizes[_pIdx]; }
   };

   // Use ANS to encode/decode literals and matches
   class ROLZCodec1 : public Function<byte> {
   public:
       ROLZCodec1(uint logPosChecks) THROW;

       ~ROLZCodec1() { delete[] _matches; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return (srcLen <= 512) ? srcLen + 64 : srcLen;
       }

   private:
       static const int MIN_MATCH = 3;
       static const int MAX_MATCH = MIN_MATCH + 255 + 7;

       int32* _matches;
       int32 _counters[65536];
       int _logPosChecks;
       int _maskChecks;
       int _posChecks;

       int findMatch(const byte buf[], const int pos, const int end);

       void emitToken(byte block[], int& idx, int litLen, int mLen);

       void readLengths(byte block[], int& idx, int& litLen, int& mLen);

       int emitLiterals(SliceArray<byte>& litBuf, byte dst[], int dstIdx, int litLen);
   };

   // Use CM (ROLZEncoder/ROLZDecoder) to encode/decode literals and matches
   // Code loosely based on 'balz' by Ilya Muravyov
   class ROLZCodec2 : public Function<byte> {
   public:
       ROLZCodec2(uint logPosChecks) THROW;

       ~ROLZCodec2() { delete[] _matches; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           // Since we do not check the dst index for each byte (for speed purpose)
           // allocate some extra buffer for incompressible data.
           return srcLen + max(srcLen >> 5, 1024);
       }

   private:
       static const int MATCH_FLAG = 0;
       static const int LITERAL_FLAG = 1;
       static const int MIN_MATCH = 3;
       static const int MAX_MATCH = MIN_MATCH + 255;

       int32* _matches;
       int32 _counters[65536];
       int _logPosChecks;
       int _maskChecks;
       int _posChecks;

       int findMatch(const byte buf[], const int pos, const int end);
   };

   class ROLZCodec : public Function<byte> {
       friend class ROLZCodec1;
       friend class ROLZCodec2;

   public:
       ROLZCodec(uint logPosChecks = LOG_POS_CHECKS2) THROW;

       ROLZCodec(Context& ctx) THROW;

       virtual ~ROLZCodec() { delete _delegate; }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return _delegate->getMaxEncodedLength(srcLen);
       }

   private:
       static const int HASH_SIZE = 1 << 16;
       static const int LOG_POS_CHECKS1 = 4;
       static const int LOG_POS_CHECKS2 = 5;
       static const int CHUNK_SIZE = 1 << 26; // 64 MB
       static const int32 HASH = 200002979;
       static const int32 HASH_MASK = ~(CHUNK_SIZE - 1);
       static const int MAX_BLOCK_SIZE = 1 << 30; // 1 GB

       Function<byte>* _delegate;

       static uint16 getKey(const byte* p)
       {
           return uint16(LittleEndian::readInt16(p));
       }

       static int32 hash(const byte* p)
       {
           return ((LittleEndian::readInt32(p) & 0x00FFFFFF) * HASH) & HASH_MASK;
       }

       static int emitCopy(byte dst[], int dstIdx, int ref, int matchLen);
   };

   inline int ROLZCodec::emitCopy(byte dst[], int dstIdx, int ref, int matchLen)
   {
       dst[dstIdx] = dst[ref];
       dst[dstIdx + 1] = dst[ref + 1];
       dst[dstIdx + 2] = dst[ref + 2];
       dstIdx += 3;
       ref += 3;

       while (matchLen >= 8) {
           dst[dstIdx] = dst[ref];
           dst[dstIdx + 1] = dst[ref + 1];
           dst[dstIdx + 2] = dst[ref + 2];
           dst[dstIdx + 3] = dst[ref + 3];
           dst[dstIdx + 4] = dst[ref + 4];
           dst[dstIdx + 5] = dst[ref + 5];
           dst[dstIdx + 6] = dst[ref + 6];
           dst[dstIdx + 7] = dst[ref + 7];
           dstIdx += 8;
           ref += 8;
           matchLen -= 8;
       }

       while (matchLen != 0) {
           dst[dstIdx++] = dst[ref++];
           matchLen--;
       }

       return dstIdx;
   }

   inline void ROLZEncoder::encodeBit(int bit)
   {
       const uint64 split = ((_high - _low) >> 4) * uint64(_probs[_pIdx][_ctx + _c1] >> 4) >> 8;

       // Update fields with new interval bounds
       if (bit == 0) {
           _low += (split + 1);
           _probs[_pIdx][_ctx + _c1] -= (_probs[_pIdx][_ctx + _c1] >> 5);
           _c1 += _c1;
       }
       else {
           _high = _low + split;
           _probs[_pIdx][_ctx + _c1] -= (((_probs[_pIdx][_ctx + _c1] - PSCALE + 32) >> 5));
           _c1 += (_c1 + 1);
       }

       // Emit unchanged first 32 bits
       while (((_low ^ _high) >> 24) == 0) {
           BigEndian::writeInt32(&_buf[_idx], int32(_high >> 32));
           _idx += 4;
           _low <<= 32;
           _high = (_high << 32) | MASK_0_32;
       }
   }

   inline int ROLZDecoder::decodeBit()
   {
       const uint64 mid = _low + (((_high - _low) >> 4) * uint64(_probs[_pIdx][_ctx + _c1] >> 4) >> 8);
       int bit;

       // Update bounds and predictor
       if (mid >= _current) {
           bit = 1;
           _high = mid;
           _probs[_pIdx][_ctx + _c1] -= ((_probs[_pIdx][_ctx + _c1] - PSCALE + 32) >> 5);
           _c1 += (_c1 + 1);
       }
       else {
           bit = 0;
           _low = mid + 1;
           _probs[_pIdx][_ctx + _c1] -= (_probs[_pIdx][_ctx + _c1] >> 5);
           _c1 += _c1;
       }

       // Read 32 bits
       while (((_low ^ _high) >> 24) == 0) {
           _low = (_low << 32) & MASK_0_56;
           _high = ((_high << 32) | MASK_0_32) & MASK_0_56;
           const uint64 val = uint64(BigEndian::readInt32(&_buf[_idx])) & MASK_0_32;
           _current = ((_current << 32) | val) & MASK_0_56;
           _idx += 4;
       }

       return bit;
   }
}
#endif
