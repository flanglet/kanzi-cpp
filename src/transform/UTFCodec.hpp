/*
Copyright 2011-2026 Frederic Langlet
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
#ifndef knz_UTFCodec
#define knz_UTFCodec

#include <climits>
#include "../Context.hpp"
#include "../Transform.hpp"


namespace kanzi
{
    // UTF8 encoder/decoder
    class UTFCodec FINAL : public Transform<byte> {
    public:
        UTFCodec() : _pCtx(nullptr), _aliasMap(nullptr), _tokens(nullptr), _tokensCap(0) {}

        UTFCodec(Context& ctx) : _pCtx(&ctx), _aliasMap(nullptr), _tokens(nullptr), _tokensCap(0) {}

        ~UTFCodec() { delete[] _aliasMap; delete[] _tokens; }

        bool forward(SliceArray<byte>& source, SliceArray<byte>& destination, int length);

        bool inverse(SliceArray<byte>& source, SliceArray<byte>& destination, int length);

        int getMaxEncodedLength(int srcLen) const { return (srcLen > INT_MAX - 8192) ? INT_MAX : srcLen + 8192; }

    private:

        static const int MIN_BLOCK_SIZE;
        static const uint8 MIN_B1[32];
        static const uint8 SPAN_B1[32];
        static const uint64 HIGH_BIT_MASK;

        Context* _pCtx;
        uint32* _aliasMap;
        uint32* _tokens;
        int _tokensCap;

        static uint64 countHighBitBytes(const byte src[], int start, int srcEnd);

        static int unpack(uint32 in, byte out[]);
   };


    inline int UTFCodec::unpack(uint32 in, byte out[]) 
    { 
       int s;
       
       switch (in >> 19) {
       case 0:
           out[0] = byte(in);
           s = 1;
           break;

       case 1:
           out[0] = byte(in >> 8);
           out[1] = byte(in);
           s = 2;
           break;

       case 2:
           out[0] = byte(((in >> 12) & 0x0F) | 0xE0);
           out[1] = byte(((in >> 6) & 0x3F) | 0x80);
           out[2] = byte((in & 0x3F) | 0x80);
           s = 3;
           break;

       case 4:
       case 5:
       case 6:
       case 7:
           out[0] = byte(((in >> 18) & 0x07) | 0xF0);
           out[1] = byte(((in >> 12) & 0x3F) | 0x80);
           out[2] = byte(((in >> 6) & 0x3F) | 0x80);
           out[3] = byte((in & 0x3F) | 0x80);
           s = 4;
           break;

       default:
           s = 0; // signal invalid value
           break;
       }

       return s; 
    }
}
#endif
