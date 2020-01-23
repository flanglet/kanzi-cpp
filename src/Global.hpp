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

#ifndef _Global_
#define _Global_

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
       static const int INV_EXP[]; //  1<<16* 1/(1 + exp(-alpha*x)) with alpha = 0.52631
       static const int LOG2_4096[]; // array with 256 elements: 4096*Math.log2(x)
       static const int LOG2[]; // array with 256 elements: int(Math.log2(x-1))

       static const int* STRETCH;
       static const int* SQUASH;

       static int squash(int d);

       static int log2(uint32 x) THROW; // fast, integer rounded

       static int _log2(uint32 x); // same as log2 minus check on input value

       static int log2_1024(uint32 x) THROW; // slow, accurate to 1/1024th
       
       static void computeJobsPerTask(int jobsPerTask[], int jobs, int tasks) THROW;

       static void computeHistogram(const byte block[], int end, uint freqs[], bool isOrder0, bool withTotal=false);

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
}
#endif
