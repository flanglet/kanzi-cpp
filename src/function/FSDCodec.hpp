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

#ifndef _FSDCodec_
#define _FSDCodec_

#include "../util.hpp" // Visual Studio min/max
#include "../Context.hpp"
#include "../Function.hpp"


// Fixed Step Delta codec
// Decorrelate values separated by a constant distance (step) and encode residuals
namespace kanzi {
   class FSDCodec : public Function<byte> {

   public:
       static const byte ESCAPE_TOKEN = byte(255);
       static const int MIN_LENGTH = 128;

       FSDCodec(){};

       FSDCodec(Context&){};

       virtual ~FSDCodec() {}

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return srcLen + max(64, srcLen >> 4); // limit expansion
       }
   };
}
#endif