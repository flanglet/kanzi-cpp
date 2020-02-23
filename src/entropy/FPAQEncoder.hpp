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

#ifndef _FPAQEncoder_
#define _FPAQEncoder_

#include "../EntropyEncoder.hpp"
#include "../Memory.hpp"
#include "../SliceArray.hpp"

namespace kanzi 
{

   // Derived from fpaq0r by Matt Mahoney & Alexander Ratushnyak.
   // See http://mattmahoney.net/dc/#fpaq0.
   // Simple (and fast) adaptive order 0 entropy coder predictor
   class FPAQEncoder : public EntropyEncoder 
   {
   private:
       static const uint64 TOP = 0x00FFFFFFFFFFFFFF;
       static const uint64 MASK_0_24 = 0x0000000000FFFFFF;
       static const uint64 MASK_0_32 = 0x00000000FFFFFFFF;
       static const int PSCALE = 65536;

       uint64 _low;
       uint64 _high;
       OutputBitStream& _bitstream;
       bool _disposed;
       bool _deallocate;
       SliceArray<byte> _sba;
       uint16 _probs[256]; // probability of bit=1
       int _ctxIdx; // previous bits


       void encodeByte(byte val);

       void encodeBit(int bit, int pred = 2048);

   protected:
       virtual void flush();

   public:
       FPAQEncoder(OutputBitStream& bitstream) THROW;

       ~FPAQEncoder();

       int encode(const byte block[], uint blkptr, uint count) THROW;

       OutputBitStream& getBitStream() const { return _bitstream; };

       void dispose();
   };


   inline void FPAQEncoder::encodeBit(int bit, int pred)
   {
       // Update probabilities
       if (bit == 0) {
          _low = _low + ((((_high - _low) >> 4) * uint64(pred)) >> 8) + 1;
          _probs[_ctxIdx] -= (_probs[_ctxIdx] >> 6);
         _ctxIdx += _ctxIdx;
       } else  {
          _high = _low + ((((_high - _low) >> 4) * uint64(pred)) >> 8);
          _probs[_ctxIdx] -= (((_probs[_ctxIdx] - PSCALE) >> 6) + 1);
          _ctxIdx += (_ctxIdx + 1);
      }

       // Write unchanged first 32 bits to bitstream
       while (((_low ^ _high) >> 24) == 0)
           flush();
   }

   inline void FPAQEncoder::flush()
   {
       BigEndian::writeInt32(&_sba._array[_sba._index], int32(_high >> 24));
       _sba._index += 4;
       _low <<= 32;
       _high = (_high << 32) | MASK_0_32;
   }
}
#endif
