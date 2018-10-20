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

#ifndef _ExpGolombDecoder_
#define _ExpGolombDecoder_

#include "../EntropyDecoder.hpp"

namespace kanzi 
{

   class ExpGolombDecoder : public EntropyDecoder 
   {
   private:
       InputBitStream& _bitstream;
       const bool _signed;

       void flush();

   public:
       ExpGolombDecoder(InputBitStream& bitstream, bool sign=true);

       ~ExpGolombDecoder() { dispose(); }

       int decode(byte arr[], uint blkptr, uint len);

       InputBitStream& getBitStream() const { return _bitstream; };

       inline byte decodeByte();

       void dispose() {}

       bool isSigned() const { return _signed; }
   };


   inline byte ExpGolombDecoder::decodeByte()
   {
       if (_bitstream.readBit() == 1)
           return 0;

       int log2 = 1;

       while (_bitstream.readBit() == 0)
           log2++;

       if (_signed == true) {
           // Decode signed: read value + sign
           byte res = byte(_bitstream.readBits(log2 + 1));
           const byte sgn = res & 1;
           res = (res >> 1) + (1 << log2) - 1;
           return byte((res - sgn) ^ -sgn); // res or -res
       }

       // Decode unsigned
       return byte((1 << log2) - 1 + _bitstream.readBits(log2));
   }
}
#endif
