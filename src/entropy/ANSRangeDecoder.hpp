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

#ifndef _ANSRangeDecoder_
#define _ANSRangeDecoder_

#include "../EntropyDecoder.hpp"
#include "../types.hpp"


// Implementation of an Asymmetric Numeral System decoder.
// See "Asymmetric Numeral System" by Jarek Duda at http://arxiv.org/abs/0902.0271
// Some code has been ported from https://github.com/rygorous/ryg_rans
// For an alternate C implementation example, see https://github.com/Cyan4973/FiniteStateEntropy

namespace kanzi
{

   class ANSDecSymbol
   {
   public:
      ANSDecSymbol()
      {
		  _freq = 0;
		  _cumFreq = 0;
      }

      ~ANSDecSymbol() { }

      void reset(int cumFreq, int freq, int logRange);

      int _cumFreq;
      int _freq;
   };


   class ANSRangeDecoder : public EntropyDecoder {
   public:
	   static const int ANS_TOP = 1 << 15; // max possible for ANS_TOP=1<<23

	   ANSRangeDecoder(InputBitStream& bitstream,
                      int order = 0,
                      int chunkSize = DEFAULT_ANS0_CHUNK_SIZE) THROW;

	   ~ANSRangeDecoder();

	   int decode(byte block[], uint blkptr, uint len);

	   InputBitStream& getBitStream() const { return _bitstream; }

	   void dispose() { _dispose(); }


   private:
	   static const int DEFAULT_ANS0_CHUNK_SIZE = 1 << 15; // 32 KB by default
	   static const int DEFAULT_LOG_RANGE = 12;
	   static const int MIN_CHUNK_SIZE = 1024;
	   static const int MAX_CHUNK_SIZE = 1 << 27; // 8*MAX_CHUNK_SIZE must not overflow

	   InputBitStream& _bitstream;
	   uint* _alphabet;
	   uint* _freqs;
	   uint8* _f2s;
	   int _f2sSize;
	   ANSDecSymbol* _symbols;
	   byte* _buffer;
	   uint _bufferSize;
	   uint _chunkSize;
	   uint _order;
	   uint _logRange;

	   void decodeChunk(byte block[], int end);

	   int decodeSymbol(byte*& p, int& st, const ANSDecSymbol& sym, const int mask);

	   int decodeHeader(uint frequencies[]);

 	   void _dispose() {}
   };


   inline void ANSDecSymbol::reset(int cumFreq, int freq, int logRange)
   {
       _cumFreq = cumFreq;
       _freq = (freq >= (1 << logRange)) ? (1 << logRange) - 1 : freq; // Mirror encoder
   }


   inline int ANSRangeDecoder::decodeSymbol(byte*& p, int& st, const ANSDecSymbol& sym, const int mask)
   {
      // Compute next ANS state
      // D(x) = (s, q_s (x/M) + mod(x,M) - b_s) where s is such b_s <= x mod M < b_{s+1}
      st = int(sym._freq * (st >> _logRange) + (st & mask) - sym._cumFreq);

      // Normalize
      while (st < ANS_TOP) {
	      st = (st << 8) | int(*p++);
	      st = (st << 8) | int(*p++);
      }

      return st;
   }

}
#endif
