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

#ifndef _CMPredictor_
#define _CMPredictor_

#include "../Predictor.hpp"

namespace kanzi 
{

   // Context model predictor based on BCM by Ilya Muravyov.
   // See https://github.com/encode84/bcm
   class CMPredictor : public Predictor 
   {
   private:
       static const int FAST_RATE = 2;
       static const int MEDIUM_RATE = 4;
       static const int SLOW_RATE = 6;

       int _c1;
       int _c2;
       int _ctx;
       int _run;
       int _idx;
       int _runMask;
       int _counter1[256][257];
       int _counter2[512][17];
       int* _pc1;
       int* _pc2;

   public:
       CMPredictor();

       ~CMPredictor(){};

       void update(int bit);

       int get();
   };

   // Update the probability model
   inline void CMPredictor::update(int bit)
   {
       _ctx <<= 1;
       int* p = &_pc2[_idx];

       if (bit == 0) {
           _pc1[256] -= (_pc1[256] >> FAST_RATE);
           _pc1[_c1] -= (_pc1[_c1] >> MEDIUM_RATE);
           *p -= (*p>> SLOW_RATE);
           p++;
           *p -= (*p>> SLOW_RATE);
       }
       else {
           _pc1[256] += ((0xFFFF - _pc1[256]) >> FAST_RATE);
           _pc1[_c1] += ((0xFFFF - _pc1[_c1]) >> MEDIUM_RATE);
           *p += ((0xFFFF - *p) >> SLOW_RATE);
           p++;
           *p += ((0xFFFF - *p) >> SLOW_RATE);
           _ctx++;
       }

       if (_ctx > 255) {
           _c2 = _c1;
           _c1 = _ctx & 0xFF;
           _ctx = 1;

           if (_c1 == _c2) {
               _run++;
               _runMask = (_run > 2) ? 256 : 0;
           }
           else {
               _run = 0;
               _runMask = 0;
           }
       }
   }

   // Return the split value representing the probability of 1 in the [0..4095] range.
   inline int CMPredictor::get()
   {
       _pc1 = _counter1[_ctx];
       const int p = (13 * _pc1[256] + 14 * _pc1[_c1] + 5 * _pc1[_c2]) >> 5;
       _idx = p >> 12;
       _pc2 = _counter2[_ctx  | _runMask];
       const int x1 = _pc2[_idx];
       const int x2 = _pc2[_idx + 1];
       const int ssep = x1 + (((x2 - x1) * (p & 4095)) >> 12);
       return (p + 3*ssep + 32) >> 6; // rescale to [0..4095]
   }
}
#endif