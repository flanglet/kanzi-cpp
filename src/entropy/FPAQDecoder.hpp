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

#ifndef _FPAQDecoder_
#define _FPAQDecoder_

#include "../EntropyDecoder.hpp"
#include "../Memory.hpp"
#include "../SliceArray.hpp"

namespace kanzi 
{

   // This class is a generic implementation of a bool entropy decoder
   class FPAQDecoder : public EntropyDecoder 
   {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_56 = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;
       static const int PSCALE = 65536;

       uint64 _low;
       uint64 _high;
       uint64 _current;
       InputBitStream& _bitstream;
       bool _initialized;
       SliceArray<byte> _sba;
       uint16 _probs[4][256]; // probability of bit=1
       uint16* _p; // pointer to current prob
       int _ctx; // previous bits

       void dispose() {};

       int decodeBit(int pred = 2048);

       bool reset();

   protected:
       virtual void initialize();

       void read();

   public:
       FPAQDecoder(InputBitStream& bitstream) THROW;

       ~FPAQDecoder();

       int decode(byte block[], uint blkptr, uint count) THROW;

       InputBitStream& getBitStream() const { return _bitstream; };

       bool isInitialized() const { return _initialized; };
   };

   
   inline int FPAQDecoder::decodeBit(int prob)
   {
       // Calculate interval split
       // Written in a way to maximize accuracy of multiplication/division
       const uint64 split = ((((_high - _low) >> 4) * uint64(prob)) >> 8) + _low;
       int bit;

       // Update probabilities
       if (split >= _current) {
           bit = 1;
           _high = split;
           _p[_ctx] -= ((_p[_ctx] - PSCALE + 64) >> 6);
           _ctx += (_ctx + 1);
       }
       else {
           bit = 0;
           _low = split + 1;
           _p[_ctx] -= (_p[_ctx] >> 6);
           _ctx += _ctx;
       }

       // Read 32 bits from bitstream
       while (((_low ^ _high) >> 24) == 0)
           read();

       return bit;
   }


   inline void FPAQDecoder::read()
   {
       _low = (_low << 32) & MASK_0_56;
       _high = ((_high << 32) | MASK_0_32) & MASK_0_56;
       const uint64 val = BigEndian::readInt32(&_sba._array[_sba._index]) & MASK_0_32;
       _current = ((_current << 32) | val) & MASK_0_56;
       _sba._index += 4;
   }
}
#endif
