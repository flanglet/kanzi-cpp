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
#include "../IllegalArgumentException.hpp"
#include "../Memory.hpp"

using namespace kanzi;

BinaryEntropyEncoder::BinaryEntropyEncoder(OutputBitStream& bitstream, Predictor* predictor, bool deallocate) THROW
: _bitstream(bitstream),
  _sba(new byte[0], 0)
{
    if (predictor == nullptr)
       throw IllegalArgumentException("Invalid null predictor parameter");

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
      throw IllegalArgumentException("Invalid block size parameter (max is 1<<30)");

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
      const int chunkSize = startChunk+length < end ? length : end-startChunk;
     
      if (_sba._length < (chunkSize * 9) >> 3) {
         delete[] _sba._array;
         _sba._array = new byte[(chunkSize * 9) >> 3];
      }
      
      _sba._index = 0;

      for (int i=startChunk; i<startChunk+chunkSize; i++)
         encodeByte(block[i]);

      EntropyUtils::writeVarInt(_bitstream, _sba._index);
      _bitstream.writeBits(&_sba._array[0], 8 * _sba._index); 
      startChunk += chunkSize;

      if (startChunk < end)         
         _bitstream.writeBits(_low | MASK_0_24, 56);         
   }

   return count;
}

inline void BinaryEntropyEncoder::encodeByte(byte val)
{
    encodeBit((val >> 7) & 1);
    encodeBit((val >> 6) & 1);
    encodeBit((val >> 5) & 1);
    encodeBit((val >> 4) & 1);
    encodeBit((val >> 3) & 1);
    encodeBit((val >> 2) & 1);
    encodeBit((val >> 1) & 1);
    encodeBit(val & 1);
}

inline void BinaryEntropyEncoder::encodeBit(int bit)
{
    // Calculate interval split
    // Written in a way to maximize accuracy of multiplication/division
    const uint64 split = (((_high - _low) >> 4) * uint64(_predictor->get())) >> 8;

    // Update fields with new interval bounds
    if (bit == 0) 
       _low = _low + split + 1;
    else 
       _high = _low + split;

    // Update predictor
    _predictor->update(bit);

    // Write unchanged first 32 bits to bitstream
    while (((_low ^ _high) & MASK_24_56) == 0)
        flush();
}

inline void BinaryEntropyEncoder::flush()
{
    BigEndian::writeInt32(&_sba._array[_sba._index], int32(_high >> 24));
    _sba._index += 4;
    _low <<= 32;
    _high = (_high << 32) | MASK_0_32;
}

void BinaryEntropyEncoder::dispose()
{
    if (_disposed == true)
        return;

    _disposed = true;
    _bitstream.writeBits(_low | MASK_0_24, 56);
}