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

#ifndef _HuffmanDecoder_
#define _HuffmanDecoder_

#include "HuffmanCommon.hpp"
#include "../EntropyDecoder.hpp"

using namespace std;

namespace kanzi 
{

   // Implementation of a static Huffman encoder.
   // Uses in place generation of canonical codes instead of a tree
   class HuffmanDecoder : public EntropyDecoder 
   {
   public:
       HuffmanDecoder(InputBitStream& bitstream, int chunkSize=HuffmanCommon::MAX_CHUNK_SIZE) THROW;

       ~HuffmanDecoder() { dispose(); delete[] _table1; };

       int readLengths() THROW;

       int decode(byte block[], uint blkptr, uint len);

       InputBitStream& getBitStream() const { return _bitstream; }

       virtual void dispose() {};

   private:
       static const int DECODING_BATCH_SIZE = 12; // in bits
       static const int TABLE0_MASK = (1 << DECODING_BATCH_SIZE) - 1;
       static const int TABLE1_MASK = (1 << (HuffmanCommon::MAX_SYMBOL_SIZE + 1)) - 1;

       InputBitStream& _bitstream;
       uint _codes[256];
       uint _alphabet[256];
       uint16 _table0[TABLE0_MASK + 1]; // small decoding table: code -> size, symbol
       uint16* _table1; // big decoding table: code -> size, symbol
       short _sizes[256];
       int _chunkSize;
       uint64 _state; // holds bits read from bitstream
       uint _bits; // hold number of unused bits in 'state'
       int _minCodeLen;

       void buildDecodingTables(int count);

       byte slowDecodeByte() THROW;

       inline byte fastDecodeByte();

       inline void fetchBits();
   };


   inline byte HuffmanDecoder::fastDecodeByte()
   {
      if (_bits < DECODING_BATCH_SIZE) 
         fetchBits();

      // Use small table
      int val = _table0[int(_state >> (_bits - DECODING_BATCH_SIZE)) & TABLE0_MASK];

      if (val == 0) {
         if (_bits < HuffmanCommon::MAX_SYMBOL_SIZE + 1) 
            fetchBits();

         // Fallback to big table
         val = _table1[int(_state >> (_bits - (HuffmanCommon::MAX_SYMBOL_SIZE + 1))) & TABLE1_MASK];
      }

      _bits -= (val >> 8);
      return byte(val);
   }

   inline void HuffmanDecoder::fetchBits()
   {
      const uint64 mask = (uint64(1) << _bits) - 1; // for _bits = 0
      _state = ((_state & mask) << (64 - _bits)) | _bitstream.readBits(64 - _bits);
      _bits = 64;
   }

}
#endif
