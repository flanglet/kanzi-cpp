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

#ifndef _NullEntropyDecoder_
#define _NullEntropyDecoder_

#include "../EntropyDecoder.hpp"
#include "../Memory.hpp"
#include "../InputBitStream.hpp"

namespace kanzi {

   // Null entropy decoder
   // Pass through that writes the data directly to the bitstream
   class NullEntropyDecoder : public EntropyDecoder {
   private:
       InputBitStream& _bitstream;

   public:
       NullEntropyDecoder(InputBitStream& bitstream);

       ~NullEntropyDecoder() {}

       int decode(byte block[], uint blkptr, uint len);

       byte decodeByte();

       InputBitStream& getBitStream() const { return _bitstream; };

       void dispose() {}
   };

   inline NullEntropyDecoder::NullEntropyDecoder(InputBitStream& bitstream)
       : _bitstream(bitstream)
   {
   }

   inline int NullEntropyDecoder::decode(byte block[], uint blkptr, uint count)
   {
      int res = 0;

      while (count > 0) {
	      const int ckSize = (count < 1<<23) ? count : 1<<23;
	      const int r = (_bitstream.readBits(&block[blkptr], 8 * ckSize) >> 3);

	      if (r == 0)
	         break;

	      res += r;
	      blkptr += r;
	      count -= r;
      }

      return res;
   }

   inline byte NullEntropyDecoder::decodeByte()
   {
      return byte(_bitstream.readBits(8));
   }
}
#endif
