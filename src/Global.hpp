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

#pragma once
#ifndef _Global_
#define _Global_

#include <cmath>
#include <cstring>
#include "util.hpp"

namespace kanzi {

   class Global {
   public:
       enum DataType { UNDEFINED, TEXT, MULTIMEDIA, EXE, NUMERIC, BASE64, DNA, BIN, UTF8 };

       static const int INV_EXP[33]; //  65536 * 1/(1 + exp(-alpha*x)) with alpha = 0.54
       static const int LOG2_4096[257]; // 4096*Math.log2(x)
       static const int LOG2[256]; // int(Math.log2(x-1))

       static const int* STRETCH;
       static const int* SQUASH;

       static int squash(int d);

       static int log2(uint32 x) THROW; // fast, integer rounded

       static int _log2(uint32 x); // same as log2 minus check on input value

       static int _log2(uint64 x); // same as log2 minus check on input value

       static int trailingZeros(uint32 x);

       static int trailingZeros(uint64 x);

       static int log2_1024(uint32 x) THROW; // slow, accurate to 1/1024th

       static void computeJobsPerTask(int jobsPerTask[], int jobs, int tasks) THROW;

       static int computeFirstOrderEntropy1024(int blockLen, uint histo[]);

       static void computeHistogram(const byte block[], int end, uint freqs[], bool isOrder0=true, bool withTotal=false);

       static DataType detectSimpleType(int count, uint histo[]);

       // Szudzik pairing
       static void encodePair(int x, int y, int& pair);

       // Szudzik pairing
       static void decodePair(int& x, int& y, int pair);

   private:
       Global() { STRETCH_BUFFER = new int[4096]; SQUASH_BUFFER = new int[4096]; }
       ~Global() { delete[] STRETCH_BUFFER; delete[] SQUASH_BUFFER; }

       int* STRETCH_BUFFER;
       int* SQUASH_BUFFER;
       static const Global _singleton;
       static const int* initStretch(int data[]);
       static const int* initSquash(int data[]);
       static char BASE64_SYMBOLS[];
       static char DNA_SYMBOLS[];
       static char NUMERIC_SYMBOLS[];
   };


   // return p = 1/(1 + exp(-d)), d scaled by 8 bits, p scaled by 12 bits
   inline int Global::squash(int d)
   {
       if (d >= 2048)
           return 4095;

       return (d <= -2048) ? 0 : SQUASH[d + 2047];
   }


   // x cannot be 0
   inline int Global::_log2(uint32 x)
   {
       #if defined(_MSC_VER)
           unsigned long res;
           _BitScanReverse(&res, x);
           return int(res);
       #elif defined(__GNUG__) 
           return 31 ^ __builtin_clz(x);
       #elif defined(__clang__)
           return 31 ^ __builtin_clz(x);
       #else
           int res = 0;

           if (x >= 1 << 16) {
              x >>= 16;
              res = 16;
           }

           if (x >= 1 << 8) {
              x >>= 8;
              res += 8;
           }

           return res + Global::LOG2[x - 1];
       #endif
   }


   // x cannot be 0
   inline int Global::_log2(uint64 x)
   {
       #if defined(_MSC_VER) && defined(_M_AMD64)
           unsigned long res;
           _BitScanReverse64(&res, x);
           return int(res);
       #elif defined(__GNUG__)
           return 63 ^ __builtin_clzll(x);
       #elif defined(__clang__)
           return 63 ^ __builtin_clzll(x);
       #else
           int res = 0;

           if (x >= uint64(1) << 32) {
              x >>= 32;
              res = 32;
           }

           if (x >= uint64(1) << 16) {
              x >>= 16;
              res += 16;
           }

           if (x >= uint64(1) << 8) {
              x >>= 8;
              res += 8;
           }

           return res + Global::LOG2[x - 1];
       #endif
   }


   // x cannot be 0
   inline int Global::trailingZeros(uint32 x)
   {
       #if defined(_MSC_VER)
           unsigned long res;
           _BitScanForward(&res, x);
           return int(res);
       #elif defined(__GNUG__)
           return __builtin_ctz(x);
       #elif defined(__clang__)
           return __builtin_ctz(x);
       #else
           return _log2((x & (~x + 1)) - 1);
       #endif
   }


   // x cannot be 0
   inline int Global::trailingZeros(uint64 x)
   {
       #if defined(_MSC_VER) && defined(_M_AMD64)
           unsigned long res;
           _BitScanForward64(&res, x);
           return int(res);
       #elif defined(__GNUG__)
           return __builtin_ctzll(x);
       #elif defined(__clang__)
           return __builtin_ctzll(x);
       #else
           return _log2((x & (~x + 1)) - 1);
       #endif
   }


   inline void Global::encodePair(int x, int y, int& pair)
   {
      pair = (x >= y) ? x * x + x + y : y * y + x;
   }


   inline void Global::decodePair(int& x, int& y, int pair)
   {
      const int s = int(sqrt(double(pair)));
      pair -= s * s;

      if (pair >= s) {
         x = s;
         y = pair - s;
      }
      else {
         x = pair;
         y = s;
      }
   }
}
#endif
