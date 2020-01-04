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

#ifndef _BinaryEntropyDecoder_
#define _BinaryEntropyDecoder_

#include "../EntropyDecoder.hpp"
#include "../Memory.hpp"
#include "../Predictor.hpp"
#include "../SliceArray.hpp"

namespace kanzi 
{

   // This class is a generic implementation of a bool entropy decoder
   class BinaryEntropyDecoder : public EntropyDecoder 
   {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_24_56 = 0x00FFFFFFFF000000;
       static const uint64 MASK_0_56 = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;

       Predictor* _predictor;
       uint64 _low;
       uint64 _high;
       uint64 _current;
       InputBitStream& _bitstream;
       bool _initialized;
       bool _deallocate;
       SliceArray<byte> _sba;

   protected:
       virtual void initialize();

       void read();

   public:
       BinaryEntropyDecoder(InputBitStream& bitstream, Predictor* predictor, bool deallocate=true) THROW;

       virtual ~BinaryEntropyDecoder();

       int decode(byte block[], uint blkptr, uint count) THROW;

       InputBitStream& getBitStream() const { return _bitstream; };

       bool isInitialized() const { return _initialized; };

       virtual void dispose() {};

       virtual byte decodeByte();

       int decodeBit(int pred = 2048);
   };

   
   inline int BinaryEntropyDecoder::decodeBit(int pred)
   {
       // Calculate interval split
       // Written in a way to maximize accuracy of multiplication/division
       const uint64 split = ((((_high - _low) >> 4) * uint64(pred)) >> 8) + _low;
       int bit;

       // Update predictor
       if (split >= _current) {
           bit = 1;
           _high = split;
           _predictor->update(1);
       }
       else {
           bit = 0;
           _low = split + 1;
           _predictor->update(0);
       }

       // Read 32 bits from bitstream
       while (((_low ^ _high) & MASK_24_56) == 0)
           read();

       return bit;
   }

   inline void BinaryEntropyDecoder::read()
   {
       _low = (_low << 32) & MASK_0_56;
       _high = ((_high << 32) | MASK_0_32) & MASK_0_56;
       const uint64 val = BigEndian::readInt32(&_sba._array[_sba._index]) & MASK_0_32;
       _current = ((_current << 32) | val) & MASK_0_56;
       _sba._index += 4;
   }

}
#endif
