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


namespace kanzi 
{

   // Implementation of a static Huffman encoder.
   // Uses in place generation of canonical codes instead of a tree
   class HuffmanDecoder : public EntropyDecoder 
   {
   public:
       HuffmanDecoder(InputBitStream& bitstream, int chunkSize=HuffmanCommon::MAX_CHUNK_SIZE) THROW;

       ~HuffmanDecoder() { dispose(); }

       int decode(byte block[], uint blkptr, uint len);

       InputBitStream& getBitStream() const { return _bitstream; }

       void dispose() {};

   private:
       static const int DECODING_BATCH_SIZE = 14; // ensures decoding table fits in L1 cache
       static const int TABLE_MASK = (1 << DECODING_BATCH_SIZE) - 1;

       InputBitStream& _bitstream;
       uint _codes[256];
       uint _alphabet[256];
       uint16 _sizes[256];
       uint16 _table[TABLE_MASK + 1]; // decoding table: code -> size, symbol
       uint64 _state; // holds bits read from bitstream
       uint8 _bits; // hold number of unused bits in 'state'
       int _chunkSize;

       int readLengths() THROW;

       void buildDecodingTable(int count);

       byte slowDecodeByte() THROW;

       byte decodeByte();

       void fetchBits();
   };


   inline byte HuffmanDecoder::decodeByte()
   {
      const uint val = uint(_table[(_state >> (_bits - DECODING_BATCH_SIZE)) & TABLE_MASK]);
      _bits -= uint8(val);
      return byte(val >> 8);
   }

   inline void HuffmanDecoder::fetchBits()
   {
      const uint64 mask = (uint64(1) << _bits) - 1; // for _bits = 0
      _state = ((_state & mask) << (64 - _bits)) | _bitstream.readBits(64 - _bits);
      _bits = 64;
   }

}
#endif
