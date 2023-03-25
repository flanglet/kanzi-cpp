/*
Copyright 2011-2022 Frederic Langlet
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
#ifndef _AliasCodec_
#define _AliasCodec_

#include "../Context.hpp"
#include "../Transform.hpp"

namespace kanzi {
    typedef struct ssAlias { uint32 val, freq; } sdAlias;

    struct SortAliasRanks {
        sdAlias* _symb;

        bool operator() (int i, int j) const
        {
            int r;
            return ((r = _symb[i].freq - _symb[j].freq) != 0) ? r > 0 : i > j;
        }

        SortAliasRanks(sdAlias symb[]) { _symb = symb; }
   };

   // Simple codec replacing 2-byte symbols with 1-byte aliases whenever possible
   class AliasCodec FINAL : public Transform<byte> {

   public:
       AliasCodec() { _pCtx = nullptr; }

       AliasCodec(Context& ctx) { _pCtx = &ctx; }

       virtual ~AliasCodec() {}

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length) THROW;


       // Required encoding output buffer size
       int getMaxEncodedLength(int srcLen) const
       {
           return srcLen + 1024;
       }

   private:
       static const int MIN_BLOCK_SIZE = 1024;

       Context* _pCtx;
   };
}

#endif
