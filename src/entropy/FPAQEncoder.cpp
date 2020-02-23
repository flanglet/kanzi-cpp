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

#include "FPAQEncoder.hpp"
#include "EntropyUtils.hpp"

using namespace kanzi;

FPAQEncoder::FPAQEncoder(OutputBitStream& bitstream) THROW
: _bitstream(bitstream),
  _sba(new byte[0], 0)
{
    _low = 0;
    _high = TOP;
    _disposed = false;

    for (int i = 0; i < 256; i++)
        _probs[i] = PSCALE >> 1;
}

FPAQEncoder::~FPAQEncoder()
{
    dispose();
    delete[] _sba._array;
}

int FPAQEncoder::encode(const byte block[], uint blkptr, uint count) THROW
{
   if (count >= 1<<30)
      throw invalid_argument("Invalid block size parameter (max is 1<<30)");

   int startChunk = blkptr;
   const int end = blkptr + count;
   int length = (count < 64) ? 64 : count;

   if (count >= 1 << 26)
   {
      // If the block is big (>=64MB), split the encoding to avoid allocating
      // too much memory.
      length = (count < (1 << 29)) ? count >> 3 : count >> 4;
   }  

   // Split block into chunks, encode chunk and write bit array to bitstream
   while (startChunk < end)
   {
      const int chunkSize = min(length, end - startChunk);
     
      if (_sba._length < (chunkSize + (chunkSize >> 3))) {
         const int length = chunkSize + (chunkSize >> 3);
         delete[] _sba._array;
         _sba._array = new byte[length];
         _sba._length = length;
      }
      
      _sba._index = 0;

      for (int i = startChunk; i < startChunk + chunkSize; i++)
         encodeByte(block[i]);

      EntropyUtils::writeVarInt(_bitstream, uint32(_sba._index));
      _bitstream.writeBits(&_sba._array[0], 8 * _sba._index);
      startChunk += chunkSize;

      if (startChunk < end)         
         _bitstream.writeBits(_low | MASK_0_24, 56);
   }

   return count;
}

void FPAQEncoder::encodeByte(byte val)
{
    const int bits = int(val) + 256;
    encodeBit(int(val) & 0x80, 1);
    encodeBit(int(val) & 0x40, bits >> 7);
    encodeBit(int(val) & 0x20, bits >> 6);
    encodeBit(int(val) & 0x10, bits >> 5);
    encodeBit(int(val) & 0x08, bits >> 4);
    encodeBit(int(val) & 0x04, bits >> 3);
    encodeBit(int(val) & 0x02, bits >> 2);
    encodeBit(int(val) & 0x01, bits >> 1);
}

void FPAQEncoder::dispose()
{
    if (_disposed == true)
        return;

    _disposed = true;
    _bitstream.writeBits(_low | MASK_0_24, 56);
}
