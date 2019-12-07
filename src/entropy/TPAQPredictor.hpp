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

#ifndef _TPAQPredictor_
#define _TPAQPredictor_

#include "../Context.hpp"
#include "../Predictor.hpp"
#include "AdaptiveProbMap.hpp"


namespace kanzi
{

   // TPAQ predictor
   // Derived from a heavily modified version of Tangelo 2.4 (by Jan Ondrus).
   // PAQ8 is written by Matt Mahoney.
   // See http://encode.ru/threads/1738-TANGELO-new-compressor-(derived-from-PAQ8-FP8)

   // Mixer combines models using neural networks with 8 inputs.
   class TPAQMixer
   {
   public:
      TPAQMixer();

      ~TPAQMixer() { }

       void update(int bit);

       int get(int32 p0, int32 p1, int32 p2, int32 p3, int32 p4, int32 p5, int32 p6, int32 p7);

   private:
       static const int BEGIN_LEARN_RATE = 60 << 7;
       static const int END_LEARN_RATE = 11 << 7;

       int32 _w0, _w1, _w2, _w3, _w4, _w5, _w6, _w7;
       int32 _p0, _p1, _p2, _p3, _p4, _p5, _p6, _p7;
       int _pr;
       int32 _skew;
       int32 _learnRate;
   };


   template <bool T>
   class TPAQPredictor : public Predictor
   {
   public:
       TPAQPredictor(Context* ctx = nullptr);

       ~TPAQPredictor();

       void update(int bit);

       // Return the split value representing the probability of 1 in the [0..4095] range.
       int get() { return _pr; }

   private:
       static const int MAX_LENGTH = 88;
       static const int BUFFER_SIZE = 64 * 1024 * 1024;
       static const int HASH_SIZE = 16 * 1024 * 1024;
       static const int MASK_BUFFER = BUFFER_SIZE - 1;
       static const int MASK_80808080 = 0x80808080;
       static const int MASK_F0F0F000 = 0xF0F0F000;
       static const int MASK_4F4FFFFF = 0x4F4FFFFF;
       static const int HASH = 0x7FEB352D;

       #define SSE0_RATE(T) ((T == true) ? 6 : 7)

       int _pr; // next predicted value (0-4095)
       int32 _c0; // bitwise context: last 0-7 bits with a leading 1 (1-255)
       int32 _c4; // last 4 whole bytes, last is in low 8 bits
       int32 _c8; // last 8 to 4 whole bytes, last is in low 8 bits
       int _bpos; // number of bits in c0 (0-7)
       int32 _pos;
       int32 _binCount;
       int32 _matchLen;
       int32 _matchPos;
       int32 _hash;
       LogisticAdaptiveProbMap<SSE0_RATE(T)> _sse0;
       LogisticAdaptiveProbMap<7> _sse1;
       TPAQMixer* _mixers;
       TPAQMixer* _mixer; // current mixer
       byte* _buffer;
       int32* _hashes; // hash table(context, buffer position)
       uint8* _bigStatesMap;// hash table(context, prediction)
       uint8* _smallStatesMap0; // hash table(context, prediction)
       uint8* _smallStatesMap1; // hash table(context, prediction)
       int32 _statesMask;
       int32 _mixersMask;
       int32 _hashMask;
       uint8* _cp0; // context pointers
       uint8* _cp1;
       uint8* _cp2;
       uint8* _cp3;
       uint8* _cp4;
       uint8* _cp5;
       uint8* _cp6;
       int32 _ctx0; // contexts
       int32 _ctx1;
       int32 _ctx2;
       int32 _ctx3;
       int32 _ctx4;
       int32 _ctx5;
       int32 _ctx6;

       inline int32 hash(int32 x, int32 y);

       inline int32 createContext(uint32 ctxId, uint32 cx);

       inline int getMatchContextPred();

       inline void findMatch();
  };


   // Adjust weights to minimize coding cost of last prediction
   inline void TPAQMixer::update(int bit)
   {
       const int32 err = (((bit << 12) - _pr) * _learnRate) >> 10;

       if (err == 0)
           return;

       // Quickly decaying learn rate
       _learnRate += ((END_LEARN_RATE - _learnRate) >> 31);
       _skew += err;

       // Train Neural Network: update weights
       _w0 += ((_p0 * err + 0) >> 12);
       _w1 += ((_p1 * err + 0) >> 12);
       _w2 += ((_p2 * err + 0) >> 12);
       _w3 += ((_p3 * err + 0) >> 12);
       _w4 += ((_p4 * err + 0) >> 12);
       _w5 += ((_p5 * err + 0) >> 12);
       _w6 += ((_p6 * err + 0) >> 12);
       _w7 += ((_p7 * err + 0) >> 12);
   }

   inline int TPAQMixer::get(int32 p0, int32 p1, int32 p2, int32 p3, int32 p4, int32 p5, int32 p6, int32 p7)
   {
       _p0 = p0;
       _p1 = p1;
       _p2 = p2;
       _p3 = p3;
       _p4 = p4;
       _p5 = p5;
       _p6 = p6;
       _p7 = p7;

       // Neural Network dot product (sum weights*inputs)
       _pr = Global::squash(((p0 * _w0) + (p1 * _w1) + (p2 * _w2) + (p3 * _w3) +
                             (p4 * _w4) + (p5 * _w5) + (p6 * _w6) + (p7 * _w7) +
                             _skew + 65536) >> 17);
       return _pr;
   }



   ///////////////////////// state table ////////////////////////
   // States represent a bit history within some context.
   // State 0 is the starting state (no bits seen).
   // States 1-30 represent all possible sequences of 1-4 bits.
   // States 31-252 represent a pair of counts, (n0,n1), the number
   //   of 0 and 1 bits respectively.  If n0+n1 < 16 then there are
   //   two states for each pair, depending on if a 0 or 1 was the last
   //   bit seen.
   // If n0 and n1 are too large, then there is no state to represent this
   // pair, so another state with about the same ratio of n0/n1 is substituted.
   // Also, when a bit is observed and the count of the opposite bit is large,
   // then part of this count is discarded to favor newer data over old.
   const uint8 STATE_TRANSITIONS[2][256] = {
       // Bit 0
       { 1, 3, 143, 4, 5, 6, 7, 8, 9, 10,
           11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
           21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
           31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
           41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
           51, 52, 47, 54, 55, 56, 57, 58, 59, 60,
           61, 62, 63, 64, 65, 66, 67, 68, 69, 6,
           71, 71, 71, 61, 75, 56, 77, 78, 77, 80,
           81, 82, 83, 84, 85, 86, 87, 88, 77, 90,
           91, 92, 80, 94, 95, 96, 97, 98, 99, 90,
           101, 94, 103, 101, 102, 104, 107, 104, 105, 108,
           111, 112, 113, 114, 115, 116, 92, 118, 94, 103,
           119, 122, 123, 94, 113, 126, 113, 128, 129, 114,
           131, 132, 112, 134, 111, 134, 110, 134, 134, 128,
           128, 142, 143, 115, 113, 142, 128, 148, 149, 79,
           148, 142, 148, 150, 155, 149, 157, 149, 159, 149,
           131, 101, 98, 115, 114, 91, 79, 58, 1, 170,
           129, 128, 110, 174, 128, 176, 129, 174, 179, 174,
           176, 141, 157, 179, 185, 157, 187, 188, 168, 151,
           191, 192, 188, 187, 172, 175, 170, 152, 185, 170,
           176, 170, 203, 148, 185, 203, 185, 192, 209, 188,
           211, 192, 213, 214, 188, 216, 168, 84, 54, 54,
           221, 54, 55, 85, 69, 63, 56, 86, 58, 230,
           231, 57, 229, 56, 224, 54, 54, 66, 58, 54,
           61, 57, 222, 78, 85, 82, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0 },
       // Bit 1
       { 2, 163, 169, 163, 165, 89, 245, 217, 245, 245,
           233, 244, 227, 74, 221, 221, 218, 226, 243, 218,
           238, 242, 74, 238, 241, 240, 239, 224, 225, 221,
           232, 72, 224, 228, 223, 225, 238, 73, 167, 76,
           237, 234, 231, 72, 31, 63, 225, 237, 236, 235,
           53, 234, 53, 234, 229, 219, 229, 233, 232, 228,
           226, 72, 74, 222, 75, 220, 167, 57, 218, 70,
           168, 72, 73, 74, 217, 76, 167, 79, 79, 166,
           162, 162, 162, 162, 165, 89, 89, 165, 89, 162,
           93, 93, 93, 161, 100, 93, 93, 93, 93, 93,
           161, 102, 120, 104, 105, 106, 108, 106, 109, 110,
           160, 134, 108, 108, 126, 117, 117, 121, 119, 120,
           107, 124, 117, 117, 125, 127, 124, 139, 130, 124,
           133, 109, 110, 135, 110, 136, 137, 138, 127, 140,
           141, 145, 144, 124, 125, 146, 147, 151, 125, 150,
           127, 152, 153, 154, 156, 139, 158, 139, 156, 139,
           130, 117, 163, 164, 141, 163, 147, 2, 2, 199,
           171, 172, 173, 177, 175, 171, 171, 178, 180, 172,
           181, 182, 183, 184, 186, 178, 189, 181, 181, 190,
           193, 182, 182, 194, 195, 196, 197, 198, 169, 200,
           201, 202, 204, 180, 205, 206, 207, 208, 210, 194,
           212, 184, 215, 193, 184, 208, 193, 163, 219, 168,
           94, 217, 223, 224, 225, 76, 227, 217, 229, 219,
           79, 86, 165, 217, 214, 225, 216, 216, 234, 75,
           214, 237, 74, 74, 163, 217, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0 }
   };

   const int32 STATE_MAP[] = {
      -31,  -400,   406,  -547,  -642,  -743,  -827,  -901,
     -901,  -974,  -945,  -955, -1060, -1031, -1044,  -956,
     -994, -1035, -1147, -1069, -1111, -1145, -1096, -1084,
    -1171, -1199, -1062, -1498, -1199, -1199, -1328, -1405,
    -1275, -1248, -1167, -1448, -1441, -1199, -1357, -1160,
    -1437, -1428, -1238, -1343, -1526, -1331, -1443, -2047,
    -2047, -2044, -2047, -2047, -2047,  -232,  -414,  -573,
     -517,  -768,  -627,  -666,  -644,  -740,  -721,  -829,
     -770,  -963,  -863, -1099,  -811,  -830,  -277, -1036,
     -286,  -218,   -42,  -411,   141, -1014, -1028,  -226,
     -469,  -540,  -573,  -581,  -594,  -610,  -628,  -711,
     -670,  -144,  -408,  -485,  -464,  -173,  -221,  -310,
     -335,  -375,  -324,  -413,   -99,  -179,  -105,  -150,
      -63,    -9,    56,    83,   119,   144,   198,   118,
      -42,   -96,  -188,  -285,  -376,   107,  -138,    38,
      -82,   186,  -114,  -190,   200,   327,    65,   406,
      108,   -95,   308,   171,   -18,   343,   135,   398,
      415,   464,   514,   494,   508,   519,    92,  -123,
      343,   575,   585,   516,    -7,  -156,   209,   574,
      613,   621,   670,   107,   989,   210,   961,   246,
      254,   -12,  -108,    97,   281,  -143,    41,   173,
     -209,   583,   -55,   250,   354,   558,    43,   274,
       14,   488,   545,    84,   528,   519,   587,   634,
      663,    95,   700,    94,  -184,   730,   742,   162,
      -10,   708,   692,   773,   707,   855,   811,   703,
      790,   871,   806,     9,   867,   840,   990,  1023,
     1409,   194,  1397,   183,  1462,   178,   -23,  1403,
      247,   172,     1,   -32,  -170,    72,  -508,   -46,
     -365,   -26,  -146,   101,   -18,  -163,  -422,  -461,
     -146,   -69,   -78,  -319,  -334,  -232,   -99,     0,
       47,   -74,     0,  -452,    14,   -57,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,
   };

   template <bool T>
   TPAQPredictor<T>::TPAQPredictor(Context* ctx)
       : _sse0(256)
       , _sse1(65536)
   {
       int statesSize = 1 << 28;
       int mixersSize = 1 << 12;
       int hashSize = HASH_SIZE;
       uint extraMem = 0;

       if (ctx != nullptr) {
           extraMem = (T == true) ? 1 : 0;

           // Block size requested by the user
           // The user can request a big block size to force more states
           const int rbsz = ctx->getInt("blockSize");

           if (rbsz >= 64 * 1024 * 1024)
               statesSize = 1 << 29;
           else if (rbsz >= 16 * 1024 * 1024)
               statesSize = 1 << 28;
           else
               statesSize = (rbsz >= 1024 * 1024) ? 1 << 27 : 1 << 26;

           // Actual size of the current block
           // Too many mixers hurts compression for small blocks.
           // Too few mixers hurts compression for big blocks.
           const int absz = ctx->getInt("size");


           if (absz >= 32 * 1024 * 1024)
               mixersSize = 1 << 17;
           else if (absz >= 16 * 1024 * 1024)
               mixersSize = 1 << 16;
           else if (absz >= 8 * 1024 * 1024)
               mixersSize = 1 << 14;
           else if (absz >= 4 * 1024 * 1024)
               mixersSize = 1 << 12;
           else
               mixersSize = (absz >= 1 * 1024 * 1024) ? 1 << 10 : 1 << 9;

       }

       mixersSize <<= extraMem;
       statesSize <<= extraMem;
       hashSize <<= (2 * extraMem);
       _pr = 2048;
       _c0 = 1;
       _c4 = 0;
       _c8 = 0;
       _pos = 0;
       _bpos = 8;
       _binCount = 0;
       _matchLen = 0;
       _matchPos = 0;
       _hash = 0;
       _mixers = new TPAQMixer[mixersSize];
       _mixer = &_mixers[0];
       _bigStatesMap = new uint8[statesSize];
       memset(_bigStatesMap, 0, statesSize);
       _smallStatesMap0 = new uint8[1 << 16];
       memset(_smallStatesMap0, 0, 1 << 16);
       _smallStatesMap1 = new uint8[1 << 24];
       memset(_smallStatesMap1, 0, 1 << 24);
       _hashes = new int32[hashSize];
       memset(_hashes, 0, sizeof(int32) * hashSize);
       _buffer = new byte[BUFFER_SIZE];
       memset(_buffer, 0, BUFFER_SIZE);
       _statesMask = statesSize - 1;
       _mixersMask = mixersSize - 1;
       _hashMask = hashSize - 1;
       _cp0 = &_smallStatesMap0[0];
       _cp1 = &_smallStatesMap1[0];
       _cp2 = &_bigStatesMap[0];
       _cp3 = &_bigStatesMap[0];
       _cp4 = &_bigStatesMap[0];
       _cp5 = &_bigStatesMap[0];
       _cp6 = &_bigStatesMap[0];
       _ctx0 = _ctx1 = _ctx2 = _ctx3 = 0;
       _ctx4 = _ctx5 = _ctx6 = 0;
   }

   template <bool T>
   TPAQPredictor<T>::~TPAQPredictor()
   {
       delete[] _bigStatesMap;
       delete[] _smallStatesMap0;
       delete[] _smallStatesMap1;
       delete[] _hashes;
       delete[] _buffer;
       delete[] _mixers;
   }

   // Update the probability model
   template <bool T>
   void TPAQPredictor<T>::update(int bit)
   {
       _mixer->update(bit);
       _bpos--;
       _c0 = (_c0 << 1) | bit;

       if (_c0 > 255) {
           _buffer[_pos & MASK_BUFFER] = byte(_c0);
           _pos++;
           _c8 = (_c8 << 8) | ((_c4 >> 24) & 0xFF);
           _c4 = (_c4 << 8) | (_c0 & 0xFF);
           _hash = (((_hash * HASH) << 4) + _c4) & _hashMask;
           _c0 = 1;
           _bpos = 8;
           _binCount += ((_c4 >> 7) & 1);

           // Select Neural Net
           _mixer = &_mixers[_c4 & _mixersMask];

           // Add contexts to NN
           _ctx0 = (_c4 & 0xFF) << 8;
           _ctx1 = (_c4 & 0xFFFF) << 8;
           _ctx2 = createContext(2, _c4 & 0x00FFFFFF);
           _ctx3 = createContext(3, _c4);

           if (_binCount < (_pos >> 2)) {
               // Mostly text or mixed
               _ctx4 = createContext(_ctx1, _c4 ^ (_c8 & 0xFFFF));
               _ctx5 = (_c8 & MASK_F0F0F000) | ((_c4 & MASK_F0F0F000) >> 4);

               if (T == true) {
                  const int h1 = ((_c4 & MASK_80808080) == 0) ? _c4 & MASK_4F4FFFFF : _c4 & MASK_80808080;
                  const int h2 = ((_c8 & MASK_80808080) == 0) ? _c8 & MASK_4F4FFFFF : _c8 & MASK_80808080;
                  _ctx6 = hash(h1 << 2, h2 >> 2);
               }
           }
           else {
               // Mostly binary
               if (T == true) {
                  _ctx6 = hash(_c4 & 0xFFFF0000, _c8 >> 16);
               }

               _ctx4 = createContext(HASH, _c4 ^ (_c4 & 0x000FFFFF));
               _ctx5 = _ctx0 | (_c8 << 16);

           }

           findMatch();

           // Keep track current position
           _hashes[_hash] = _pos;
       }

       // Get initial predictions
       // It has been observed that accessing memory via [ctx ^ c] is significantly faster
       // on SandyBridge/Windows and slower on SkyLake/Linux except when [ctx & 255 == 0]
       // (with c < 256). Hence, use XOR for _ctx5 which is the only context that fullfills
       // the condition.
       prefetchRead(&_bigStatesMap[(_ctx2 + _c0) & _statesMask]);
       prefetchRead(&_bigStatesMap[(_ctx3 + _c0) & _statesMask]);
       prefetchRead(&_bigStatesMap[(_ctx4 + _c0) & _statesMask]);
       prefetchRead(&_bigStatesMap[(_ctx5 ^ _c0) & _statesMask]);
       prefetchRead(&_bigStatesMap[(_ctx6 + _c0) & _statesMask]);

       const uint8* table = STATE_TRANSITIONS[bit];
       *_cp0 = table[*_cp0];
       *_cp1 = table[*_cp1];
       *_cp2 = table[*_cp2];
       *_cp3 = table[*_cp3];
       *_cp4 = table[*_cp4];
       *_cp5 = table[*_cp5];
       _cp0 = &_smallStatesMap0[_ctx0 + _c0];
       const int p0 = STATE_MAP[*_cp0];
       _cp1 = &_smallStatesMap1[_ctx1 + _c0];
       const int p1 = STATE_MAP[*_cp1];
       _cp2 = &_bigStatesMap[(_ctx2 + _c0) & _statesMask];
       const int p2 = STATE_MAP[*_cp2];
       _cp3 = &_bigStatesMap[(_ctx3 + _c0) & _statesMask];
       const int p3 = STATE_MAP[*_cp3];
       _cp4 = &_bigStatesMap[(_ctx4 + _c0) & _statesMask];
       const int p4 = STATE_MAP[*_cp4];
       _cp5 = &_bigStatesMap[(_ctx5 ^ _c0) & _statesMask];
       const int p5 = STATE_MAP[*_cp5];

       const int p7 = (_matchLen == 0) ? 0 : getMatchContextPred();
       int p;

       if (T == false) {
          // Mix predictions using NN
          p = _mixer->get(p0, p1, p2, p3, p4, p5, p7, p7);

          // SSE (Secondary Symbol Estimation)
          if (_binCount < (_pos >> 3)) {
              p = _sse0.get(bit, p, _c0);
          }
       } else {
          // One more prediction
          *_cp6 = table[*_cp6];
          _cp6 = &_bigStatesMap[(_ctx6 + _c0) & _statesMask];
          const int p6 = STATE_MAP[*_cp6];

          // Mix predictions using NN
          p = _mixer->get(p0, p1, p2, p3, p4, p5, p6, p7);

          // SSE (Secondary Symbol Estimation)
          if (_binCount < (_pos >> 3)) {
              p = _sse1.get(bit, p, _ctx0 + _c0);
          }
          else {
              if (_binCount >= (_pos >> 2))
                 p = (3 * _sse0.get(bit, p, _c0) + p) >> 2;

              p = (3 * _sse1.get(bit, p, _ctx0 + _c0) + p) >> 2;
          }
       }

       _pr = p + (uint32(p - 2048) >> 31);
   }

   template <bool T>
   void TPAQPredictor<T>::findMatch()
   {
       // Update ongoing sequence match or detect match in the buffer (LZ like)
       if (_matchLen > 0) {
           if (_matchLen < MAX_LENGTH)
               _matchLen++;

           _matchPos++;
       }
       else {
           // Retrieve match position
           _matchPos = _hashes[_hash];

           // Detect match
           if ((_matchPos != 0) && (_pos - _matchPos <= MASK_BUFFER)) {
               int r = _matchLen + 2;

               while (r <= MAX_LENGTH) {
                   if ((_buffer[(_pos - r) & MASK_BUFFER]) != (_buffer[(_matchPos - r) & MASK_BUFFER]))
                       break;

                   if ((_buffer[(_pos - r - 1) & MASK_BUFFER]) != (_buffer[(_matchPos - r - 1) & MASK_BUFFER]))
                       break;

                   r += 2;
               }

               _matchLen = r - 2;
           }
       }
   }

   template <bool T>
   inline int32 TPAQPredictor<T>::hash(int32 x, int32 y)
   {
       const int32 h = x * HASH ^ y * HASH;
       return (h >> 1) ^ (h >> 9) ^ (x >> 2) ^ (y >> 3) ^ HASH;
   }

   template <bool T>
   inline int32 TPAQPredictor<T>::createContext(uint32 ctxId, uint32 cx)
   {
       cx = cx * 987654323 + ctxId;
       cx = (cx << 16) | (cx >> 16);
       return cx * 123456791 + ctxId;
   }

   // Get a prediction from the match model in [-2047..2048]
   template <bool T>
   inline int TPAQPredictor<T>::getMatchContextPred()
   {
       if (_c0 == ((int(_buffer[_matchPos & MASK_BUFFER]) & 0xFF) | 256) >> _bpos) {
           int p = (_matchLen <= 24) ? _matchLen : 24 + ((_matchLen - 24) >> 3);

           if ((int(_buffer[_matchPos & MASK_BUFFER] >> (_bpos - 1)) & 1) == 0)
               p = -p;

           return p << 6;
       }

       _matchLen = 0;
       return 0;
   }
}
#endif