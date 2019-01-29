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

       ~HuffmanDecoder() { dispose(); };

       int readLengths() THROW;

       int decode(byte block[], uint blkptr, uint len);

       InputBitStream& getBitStream() const { return _bitstream; }

       void dispose(){};

   private:
       static const int DECODING_BATCH_SIZE = 12; // in bits
       static const int DECODING_MASK = (1 << DECODING_BATCH_SIZE) - 1;
       static const int MAX_DECODING_INDEX = (DECODING_BATCH_SIZE << 8) | 0xFF;
       static const int SYMBOL_ABSENT = 0x7FFFFFFF;

       InputBitStream& _bitstream;
       uint _codes[256];
       uint _alphabet[256];
       uint16 _fdTable[1 << DECODING_BATCH_SIZE]; // Fast decoding table
       uint16 _sdTable[256]; // Slow decoding table
       int _sdtIndexes[HuffmanCommon::MAX_SYMBOL_SIZE + 1]; // Indexes for slow decoding table
       short _sizes[256];
       int _chunkSize;
       uint64 _state; // holds bits read from bitstream
       uint _bits; // hold number of unused bits in 'state'
       int _minCodeLen;

       void buildDecodingTables(int count);

       byte slowDecodeByte(int code, int codeLen) THROW;

       inline byte fastDecodeByte();
   };

}
#endif
