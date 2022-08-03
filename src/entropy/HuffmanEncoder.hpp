/*
Copyright 2011-2022 Frederic Langlet
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

#pragma once
#ifndef _HuffmanEncoder_
#define _HuffmanEncoder_

#include "HuffmanCommon.hpp"
#include "../EntropyEncoder.hpp"


namespace kanzi
{

   // Implementation of a static Huffman encoder.
   // Uses in place generation of canonical codes instead of a tree
   class HuffmanEncoder : public EntropyEncoder
   {
   private:
       OutputBitStream& _bitstream;
       uint _freqs[256];
       uint _codes[256];
       uint _sranks[256]; // sorted ranks
       int _chunkSize;

       uint computeCodeLengths(uint frequencies[], uint16 sizes[], uint alphabet[], int count) THROW;

       bool reset();

       void _dispose() {}

       static void computeInPlaceSizesPhase1(uint data[], int n);

       static void computeInPlaceSizesPhase2(uint data[], int n);

   public:
       HuffmanEncoder(OutputBitStream& bitstream, int chunkSize = HuffmanCommon::MAX_CHUNK_SIZE) THROW;

       ~HuffmanEncoder() { _dispose(); }

       int updateFrequencies(uint frequencies[]) THROW;

       int encode(const byte block[], uint blkptr, uint len);

       OutputBitStream& getBitStream() const { return _bitstream; }

       void dispose() { _dispose(); }
   };

}
#endif
