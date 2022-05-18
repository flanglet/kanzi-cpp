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

#ifndef _UTFCodec_
#define _UTFCodec_

#include "../Context.hpp"
#include "../Transform.hpp"


namespace kanzi
{
    typedef struct ss { uint32 sym, freq; } sd;

    struct SortRanks {
            sd* _symb;
            
            bool operator() (int i, int j) const { return _symb[i].freq < _symb[j].freq; }
            
            SortRanks(sd symb[]) { _symb = symb; };
    };

    
    // UTF8 encoder/decoder
    class UTFCodec : public Transform<byte> {
    public:
        UTFCodec() { _pCtx = nullptr; }

        UTFCodec(Context& ctx) { _pCtx = &ctx; }

        ~UTFCodec() {}

        bool forward(SliceArray<byte>& source, SliceArray<byte>& destination, int length) THROW;

        bool inverse(SliceArray<byte>& source, SliceArray<byte>& destination, int length) THROW;

        int getMaxEncodedLength(int srcLen) const { return srcLen + ((srcLen < 32768) ? 4096 : srcLen / 10); }

    private:

        static const int MIN_BLOCK_SIZE = 1024;
        static const int SIZES[16];

        Context* _pCtx;
       
        static bool validate(byte src[], int count);

        static int pack(byte in[], uint32& out);

        static int unpack(uint32 in, byte out[]);
   };


    inline int UTFCodec::pack(byte in[], uint32& out) 
    {   
       int s = SIZES[uint8(in[0]) >> 4];

       switch (s) {
       case 1:
           out = uint32(in[0]);
           break;

       case 2:
	       out = (1 << 21) | (uint32(in[0]) << 8) | uint32(in[1]);
           break; 

       case 3:
           out = (2 << 21) | ((uint32(in[0]) & 0x0F) << 12) | ((uint32(in[1]) & 0x3F) << 6) | (uint32(in[2]) & 0x3F);
           break;

       case 4:
           out = (3 << 21) | ((uint32(in[0]) & 0x07) << 18) | ((uint32(in[1]) & 0x3F) << 12) | ((uint32(in[2]) & 0x3F) << 6) | (uint32(in[3]) & 0x3F);
           break;

       default:
           out = 0;
           s = 0; // signal invalid value
           break;
       }

       return s; 
    }


    inline int UTFCodec::unpack(uint32 in, byte out[]) 
    { 
       int s = int(in >> 21) + 1;
       
       switch (s) {
       case 1:
           out[0] = byte(in);
           break;

       case 2:
           out[0] = byte(in >> 8);
           out[1] = byte(in);
           break;

       case 3:
           out[0] = byte(((in >> 12) & 0x0F) | 0xE0);
           out[1] = byte(((in >> 6) & 0x3F) | 0x80);
           out[2] = byte((in & 0x3F) | 0x80);
           break;

       case 4:	  
           out[0] = byte(((in >> 18) & 0x07) | 0xF0);
           out[1] = byte(((in >> 12) & 0x3F) | 0x80);
           out[2] = byte(((in >> 6) & 0x3F) | 0x80);
           out[3] = byte((in & 0x3F) | 0x80);
           break;

       default:
           s = 0; // signal invalid value
           break;
       }

       return s; 
    }
}
#endif
