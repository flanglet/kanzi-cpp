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

#ifndef _Global_
#define _Global_

#include <cmath>
#include <cstring>
#include "types.hpp"
#include "util.hpp"

#if defined(_MSC_VER)
   #include <intrin.h>
#elif defined(__clang__)
   #ifdef __x86_64__
      #include <x86intrin.h>
   #endif
#endif

namespace kanzi {

   class Global {
   public:
       enum DataType { UNDEFINED, TEXT, MULTIMEDIA, X86, NUMERIC, BASE64, DNA, BIN };

       static const int INV_EXP[33]; //  65536 * 1/(1 + exp(-alpha*x)) with alpha = 0.54
       static const int LOG2_4096[257]; // 4096*Math.log2(x)
       static const int LOG2[256]; // int(Math.log2(x-1))

       static const int* STRETCH;
       static const int* SQUASH;

       static int squash(int d);

       static int log2(uint32 x) THROW; // fast, integer rounded

       static int _log2(uint32 x); // same as log2 minus check on input value

       static int log2_1024(uint32 x) THROW; // slow, accurate to 1/1024th

       static void computeJobsPerTask(int jobsPerTask[], int jobs, int tasks) THROW;

       static int computeFirstOrderEntropy1024(int blockLen, uint histo[]);

       static void computeHistogram(const byte block[], int end, uint freqs[], bool isOrder0=true, bool withTotal=false);

       // Szudzik pairing
       static void encodePair(int x, int y, int& res);

       // Szudzik pairing
       static void decodePair(int& x, int& y, int res);

   private:
       Global() { STRETCH_BUFFER = new int[4096]; SQUASH_BUFFER = new int[4096]; }
       ~Global() { delete[] STRETCH_BUFFER; delete[] SQUASH_BUFFER; }

       int* STRETCH_BUFFER;
       int* SQUASH_BUFFER;
       static const Global _singleton;
       static const int* initStretch(int data[]);
       static const int* initSquash(int data[]);
   };


   // return p = 1/(1 + exp(-d)), d scaled by 8 bits, p scaled by 12 bits
   inline int Global::squash(int d)
   {
       if (d >= 2048)
           return 4095;

       return (d <= -2048) ? 0 : SQUASH[d + 2047];
   }


   inline int Global::_log2(uint32 x)
   {
       #if defined(_MSC_VER)
           int res;
           _BitScanReverse((unsigned long*) &res, x);
           return res;
       #elif defined(__GNUG__)
           return 31 - __builtin_clz(x);
       #elif defined(__clang__)
           return 31 - __lzcnt32(x);
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


   inline void Global::encodePair(int x, int y, int& res)
   {
      res = (x >= y) ? x * x + x + y : y * y + x;
   }


   inline void Global::decodePair(int& x, int& y, int res)
   {
      const int s = int(sqrt(double(res)));
      res -= s * s;

      if (res >= s) {
         x = s;
         y = res - s;
      }
      else {
         x = res;
         y = s;
      }
   }
}
#endif
