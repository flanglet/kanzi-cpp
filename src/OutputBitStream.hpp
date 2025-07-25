/*
Copyright 2011-2025 Frederic Langlet
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

#pragma once
#ifndef _OutputBitStream_
#define _OutputBitStream_

#include "types.hpp"

namespace kanzi
{

   class OutputBitStream
   {
   public:
       // Write the least significant bit of the input integer
       // Throws if the stream is closed.
       virtual void writeBit(int bit) = 0;

       // Length is the number of bits in [1..64]. Return the number of bits written.
       // Throws if the stream is closed.
       virtual uint writeBits(uint64 bits, uint length) = 0;

       // Write bits ouf of the byte array. Length is the number of bits.
       // Return the number of bits written.
       // Throws if the stream is closed.
       virtual uint writeBits(const byte bits[], uint length) = 0;

       virtual void close() = 0;

       // Number of bits written
       virtual uint64 written() const = 0;

       OutputBitStream(){}

       virtual ~OutputBitStream(){}
   };

}
#endif

