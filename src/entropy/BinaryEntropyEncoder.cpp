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

#include "BinaryEntropyEncoder.hpp"
#include "EntropyUtils.hpp"

using namespace kanzi;

BinaryEntropyEncoder::BinaryEntropyEncoder(OutputBitStream& bitstream, Predictor* predictor, bool deallocate) THROW
: _bitstream(bitstream),
  _sba(new byte[0], 0)
{
    if (predictor == nullptr)
       throw invalid_argument("Invalid null predictor parameter");

    _predictor = predictor;
    _low = 0;
    _high = TOP;
    _disposed = false;
    _deallocate = deallocate;
}

BinaryEntropyEncoder::~BinaryEntropyEncoder()
{
    dispose();
    delete[] _sba._array;

    if (_deallocate)
       delete _predictor;
}

int BinaryEntropyEncoder::encode(byte block[], uint blkptr, uint count) THROW
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
         delete[] _sba._array;
         _sba._array = new byte[chunkSize + (chunkSize >> 3)];
      }
      
      _sba._index = 0;

      for (int i = startChunk; i< startChunk + chunkSize; i++)
         encodeByte(block[i]);

      EntropyUtils::writeVarInt(_bitstream, uint32(_sba._index));
      _bitstream.writeBits(&_sba._array[0], 8 * _sba._index);
      startChunk += chunkSize;

      if (startChunk < end)         
         _bitstream.writeBits(_low | MASK_0_24, 56);
   }

   return count;
}

void BinaryEntropyEncoder::encodeByte(byte val)
{
    encodeBit(int(val >> 7) & 1);
    encodeBit(int(val >> 6) & 1);
    encodeBit(int(val >> 5) & 1);
    encodeBit(int(val >> 4) & 1);
    encodeBit(int(val >> 3) & 1);
    encodeBit(int(val >> 2) & 1);
    encodeBit(int(val >> 1) & 1);
    encodeBit(int(val) & 1);
}

void BinaryEntropyEncoder::dispose()
{
    if (_disposed == true)
        return;

    _disposed = true;
    _bitstream.writeBits(_low | MASK_0_24, 56);
}
