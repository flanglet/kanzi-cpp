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

#ifndef _BinaryEntropyEncoder_
#define _BinaryEntropyEncoder_

#include "../EntropyEncoder.hpp"
#include "../Memory.hpp"
#include "../Predictor.hpp"
#include "../SliceArray.hpp"

namespace kanzi 
{

   // This class is a generic implementation of a bool entropy encoder
   class BinaryEntropyEncoder : public EntropyEncoder 
   {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_24 = 0x0000000000FFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;

       Predictor* _predictor;
       uint64 _low;
       uint64 _high;
       OutputBitStream& _bitstream;
       bool _disposed;
       bool _deallocate;
       SliceArray<byte> _sba;

   protected:
       virtual void flush();

   public:
       BinaryEntropyEncoder(OutputBitStream& bitstream, Predictor* predictor, bool deallocate=true) THROW;

       virtual ~BinaryEntropyEncoder();

       int encode(byte block[], uint blkptr, uint count) THROW;

       OutputBitStream& getBitStream() const { return _bitstream; };

       virtual void dispose();

       virtual void encodeByte(byte val);

       inline void encodeBit(int bit);
   };


   inline void BinaryEntropyEncoder::encodeBit(int bit)
   {
       // Calculate interval split
       // Written in a way to maximize accuracy of multiplication/division
       const uint64 split = (((_high - _low) >> 4) * uint64(_predictor->get())) >> 8;

       // Update fields with new interval bounds
       if (bit == 0) 
          _low += (split + 1);
       else 
          _high = _low + split;

       // Update predictor
       _predictor->update(bit);

       // Write unchanged first 32 bits to bitstream
       while (((_low ^ _high) >> 24) == 0)
           flush();
   }

   inline void BinaryEntropyEncoder::flush()
   {
       BigEndian::writeInt32(&_sba._array[_sba._index], int32(_high >> 24));
       _sba._index += 4;
       _low <<= 32;
       _high = (_high << 32) | MASK_0_32;
   }
}
#endif