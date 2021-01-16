/*
Copyright 2011-2021 Frederic Langlet
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

#ifndef _X86Codec_
#define _X86Codec_

#include "../Context.hpp"
#include "../Function.hpp"

namespace kanzi
{
   // Adapted from MCM: https://github.com/mathieuchartier/mcm/blob/master/X86Binary.hpp
   class X86Codec : public Function<byte> {
   public:
       X86Codec() { _pCtx = nullptr; }

       X86Codec(Context& ctx) { _pCtx = &ctx; }

       ~X86Codec() {}

       bool forward(SliceArray<byte>& source, SliceArray<byte>& destination, int length) THROW;

       bool inverse(SliceArray<byte>& source, SliceArray<byte>& destination, int length) THROW;

       int getMaxEncodedLength(int inputLen) const;

   private:

      static const byte MASK_JUMP = byte(0xFE);
      static const byte INSTRUCTION_JUMP = byte(0xE8);
      static const byte INSTRUCTION_JCC = byte(0x80);
      static const byte PREFIX_JCC = byte(0x0F);
      static const byte MASK_JCC = byte(0xF0);
      static const byte MASK_ADDRESS = byte(0xD5);
      static const byte ESCAPE = byte(0xF5);

      bool isExeBlock(byte src[], int end, int count) const;

       Context* _pCtx;
   };

}
#endif
