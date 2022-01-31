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

#ifndef _Magic_
#define _Magic_

#include "Memory.hpp"


namespace kanzi
{
   class Magic {
   public:
       static const uint NO_MAGIC = 0;
       static const uint JPG_MAGIC = 0xFFD8FFE0;
       static const uint GIF_MAGIC = 0x47494638;
       static const uint PDF_MAGIC = 0x25504446;
       static const uint ZIP_MAGIC = 0x504B0304; // Works for jar & office docs
       static const uint LZMA_MAGIC = 0x377ABCAF;
       static const uint PNG_MAGIC = 0x89504E47;
       static const uint ELF_MAGIC = 0x7F454C46;
       static const uint MAC_MAGIC32 = 0xFEEDFACE;
       static const uint MAC_CIGAM32 = 0xCEFAEDFE;
       static const uint MAC_MAGIC64 = 0xFEEDFACF;
       static const uint MAC_CIGAM64 = 0xCFFAEDFE;
       static const uint ZSTD_MAGIC = 0x28B52FFD;
       static const uint BROTLI_MAGIC = 0x81CFB2CE;
       static const uint RIFF_MAGIC = 0x04524946;
       static const uint CAB_MAGIC = 0x4D534346;
       static const uint GZIP_MAGIC = 0x1F8B;
       static const uint BMP_MAGIC = 0x424D;
       static const uint WIN_MAGIC = 0x4D5A;
       
       static uint getType(const byte src[]);
       
       
    private:
       Magic() {}
       ~Magic() {}        
       
    }; 
    
       
    inline uint Magic::getType(const byte src[]) 
    {
        static const uint KEYS32[14] = { 
             GIF_MAGIC, PDF_MAGIC, ZIP_MAGIC, LZMA_MAGIC, PNG_MAGIC,
             ELF_MAGIC, MAC_MAGIC32, MAC_CIGAM32, MAC_MAGIC64, MAC_CIGAM64,
             ZSTD_MAGIC, BROTLI_MAGIC, CAB_MAGIC, RIFF_MAGIC
        };

        static const uint KEYS16[3] = { 
             GZIP_MAGIC, BMP_MAGIC, WIN_MAGIC
        };
    
        uint key = uint(BigEndian::readInt32(&src[0]));
       
        if ((key & ~0x0F) == JPG_MAGIC)
            return key;
       
        for (int i = 0; i < 14; i++) {
            if (key == KEYS32[i])
               return key;
        }
        
        for (int i = 0; i < 3; i++) {
            if ((key >> 16) == KEYS16[i])
                return key >> 16;
        }     

        return NO_MAGIC;      
    }

}
    
#endif      