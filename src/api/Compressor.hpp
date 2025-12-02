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
#ifndef _Compressor_
#define _Compressor_

#ifdef _WIN32
   #define CDECL __cdecl

   #ifdef ARCHIVER_EXPORTS
      #define ARCHIVER_API __declspec(dllexport)
   #else
      #define ARCHIVER_API __declspec(dllimport)
   #endif
#else
   #define CDECL
   #define ARCHIVER_API
#endif

#include <stdio.h>


#define ARCHIVER_VERSION_STRING "1.0.0"


#ifdef __cplusplus
   extern "C" {
#endif

   /**
    *  Compression context: encapsulates compressor state (opaque: could change in future versions)
    */
   struct cContext;


   /**
    *  Compression parameters
    */
   struct cData {
       char transform[64];          /* name of transforms [None|PACK|BWT|BWTS|LZ|LZX|LZP|ROLZ|ROLZX]
                                                          [RLT|ZRLT|MTFT|RANK|SRT|TEXT|MM|EXE|UTF|DNA] */
       char entropy[16];            /* name of entropy codec [None|Huffman|ANS0|ANS1|Range|FPAQ|TPAQ|TPAQX|CM] */
       size_t blockSize;            /* size of block in bytes */
       unsigned int jobs;           /* max number of concurrent tasks */
       int checksum;                /* 0, 32 or 64 to indicate size of block checksum */
       int headerless;              /* bool to indicate if the bitstream has a header (usually set to 0) */
   };


   /**
    * @return the version number of the library.
    * Useful for checking for compatibility at runtime.
    */
   ARCHIVER_API unsigned int CDECL Archiver_GetVersion(void);


   /**
    *  Initialize the compressor internal states.
    *
    *  @param cParam [IN|OUT] - the compression parameters, transform and enropy are validated and rewritten
    *  @param dst [IN] - the destination stream of compressed data
    *  @param ctx [IN|OUT] - pointer to the compression context created by the call
    *
    *  @return 0 in case of success, else see error code in Error.hpp
    */
   ARCHIVER_API int CDECL initCompressor(struct cData* cParam, FILE* dst, struct cContext** ctx);

    /**
    *  Compress a block of data. The compressor must have been initialized.
    *
    *  @param ctx [IN] - the compression context created during initialization
    *  @param src [IN] - the source block of data to compress
    *  @param inSize [IN] - the size of the source block to compress.
    *  @param outSize [IN|OUT] - the size of the compressed data
                              Updated to reflect the number bytes written to the destination.
    *
    *  @return 0 in case of success, else see error code in Error.hpp
    */
   ARCHIVER_API int CDECL compress(struct cContext* ctx, const unsigned char* src, size_t inSize, size_t* outSize);

   /**
    *  Dispose the compressor and cleanup memory resources.
    *
    *  @param ctx [IN] - the compression context created during initialization
    *  @param outSize [IN|OUT] - the number of bytes written to the destination
    *                            (the compressor may flush internal data)
    *
    *  @return 0 in case of success, else see error code in Error.hpp
    */
   ARCHIVER_API int CDECL disposeCompressor(struct cContext** ctx, size_t* outSize);

#ifdef __cplusplus
   }
#endif


#endif

