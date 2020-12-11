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

//#include <sstream>
#include "FSDCodec.hpp"
#include "FunctionFactory.hpp"
#include "../Global.hpp"

using namespace kanzi;
using namespace std;

const uint8 FSDCodec::ZIGZAG[255] = {
	   253,   251,   249,   247,   245,   243,   241,   239,
	   237,   235,   233,   231,   229,   227,   225,   223,
	   221,   219,   217,   215,   213,   211,   209,   207,
	   205,   203,   201,   199,   197,   195,   193,   191,
	   189,   187,   185,   183,   181,   179,   177,   175,
	   173,   171,   169,   167,   165,   163,   161,   159,
	   157,   155,   153,   151,   149,   147,   145,   143,
	   141,   139,   137,   135,   133,   131,   129,   127,
	   125,   123,   121,   119,   117,   115,   113,   111,
	   109,   107,   105,   103,   101,    99,    97,    95,
	    93,    91,    89,    87,    85,    83,    81,    79,
	    77,    75,    73,    71,    69,    67,    65,    63,
	    61,    59,    57,    55,    53,    51,    49,    47,
	    45,    43,    41,    39,    37,    35,    33,    31,
	    29,    27,    25,    23,    21,    19,    17,    15,
	    13,    11,     9,     7,     5,     3,     1,     0,
	     2,     4,     6,     8,    10,    12,    14,    16,
	    18,    20,    22,    24,    26,    28,    30,    32,
	    34,    36,    38,    40,    42,    44,    46,    48,
	    50,    52,    54,    56,    58,    60,    62,    64,
	    66,    68,    70,    72,    74,    76,    78,    80,
	    82,    84,    86,    88,    90,    92,    94,    96,
	    98,   100,   102,   104,   106,   108,   110,   112,
	   114,   116,   118,   120,   122,   124,   126,   128,
	   130,   132,   134,   136,   138,   140,   142,   144,
	   146,   148,   150,   152,   154,   156,   158,   160,
	   162,   164,   166,   168,   170,   172,   174,   176,
	   178,   180,   182,   184,   186,   188,   190,   192,
	   194,   196,   198,   200,   202,   204,   206,   208,
	   210,   212,   214,   216,   218,   220,   222,   224,
	   226,   228,   230,   232,   234,   236,   238,   240,
	   242,   244,   246,   248,   250,   252,   254
};

bool FSDCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("FSD codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("FSD codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_LENGTH)
        return false;

    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    const int srcEnd = count;
    const int dstEnd = output._length;
    const int count5 = count / 5;
    const int count10 = count / 10;
    byte* in;
    byte* dst1 = &dst[0];
    byte* dst2 = &dst[count5 * 1];
    byte* dst3 = &dst[count5 * 2];
    byte* dst4 = &dst[count5 * 3];
    byte* dst8 = &dst[count5 * 4];

    // Check several step values on a sub-block (no memory allocation)
    // Sample 2 sub-blocks
    in = &src[count5 * 3];

    for (int i = 0; i < count10; i++) {
        const byte b = in[i];
        dst1[i] = b ^ in[i - 1];
        dst2[i] = b ^ in[i - 2];
        dst3[i] = b ^ in[i - 3];
        dst4[i] = b ^ in[i - 4];
        dst8[i] = b ^ in[i - 8];
    }

    in = &src[count5 * 1];

    for (int i = count10; i < count5; i++) {
        const byte b = in[i];
        dst1[i] = b ^ in[i - 1];
        dst2[i] = b ^ in[i - 2];
        dst3[i] = b ^ in[i - 3];
        dst4[i] = b ^ in[i - 4];
        dst8[i] = b ^ in[i - 8];
    }

    // Find if entropy is lower post transform
    uint histo[256];
    int ent[6];
    const int count3 = count / 3;
    Global::computeHistogram(&src[count3], count3, histo);
    ent[0] = Global::computeFirstOrderEntropy1024(count3, histo);
    Global::computeHistogram(&dst1[0], count5, histo);
    ent[1] = Global::computeFirstOrderEntropy1024(count5, histo);
    Global::computeHistogram(&dst2[0], count5, histo);
    ent[2] = Global::computeFirstOrderEntropy1024(count5, histo);
    Global::computeHistogram(&dst3[0], count5, histo);
    ent[3] = Global::computeFirstOrderEntropy1024(count5, histo);
    Global::computeHistogram(&dst4[0], count5, histo);
    ent[4] = Global::computeFirstOrderEntropy1024(count5, histo);
    Global::computeHistogram(&dst8[0], count5, histo);
    ent[5] = Global::computeFirstOrderEntropy1024(count5, histo);

    int minIdx = 0;

    for (int i = 1; i < 6; i++) {
        if (ent[i] < ent[minIdx])
            minIdx = i;
    }

    if (minIdx == 0)
        return false;

    // If not 'better enough', quick exit
    if ((_isFast == true) && (ent[minIdx] >= ((123 * ent[0]) >> 7)))
        return false;

    const int dist = (minIdx <= 4) ? minIdx : 8;
    int largeDeltas = 0;

    // Detect best coding by sampling for large deltas
    for (int i = 2 * count5; i < 3 * count5; i++) {
        const int delta = int(src[i]) - int(src[i - dist]);

        if ((delta < -127) || (delta > 127))
            largeDeltas++;
    }

    // Delta coding works better for pictures & xor coding better for wav files
    // Select xor coding if large deltas are over 3% (ad-hoc threshold)
    const byte mode = (largeDeltas > (count5 >> 5)) ? XOR_CODING : DELTA_CODING;
    dst[0] = mode;
    dst[1] = byte(dist);
    int srcIdx = 0;
    int dstIdx = 2;

    // Emit first bytes
    for (int i = 0; i < dist; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Emit modified bytes
    if (mode == DELTA_CODING) {
        while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
            const int delta = int(src[srcIdx]) - int(src[srcIdx - dist]);

            if ((delta >= -127) && (delta <= 127)) {
                dst[dstIdx++] = byte(ZIGZAG[delta+127]); // zigzag encode delta
                srcIdx++;
                continue;
            }

            if (dstIdx == dstEnd - 1)
                break;

            // Skip delta, encode with escape
            dst[dstIdx++] = ESCAPE_TOKEN;
            dst[dstIdx++] = src[srcIdx] ^ src[srcIdx - dist];
            srcIdx++;
        }
    }
    else { // mode == XOR_CODING
        while (srcIdx < srcEnd) {
            dst[dstIdx++] = src[srcIdx] ^ src[srcIdx - dist];
            srcIdx++;
        }
    }

    if (srcIdx != srcEnd)
        return false;

    // Extra check that the transform makes sense
    const int length = (_isFast == true) ? dstIdx >> 1 : dstIdx;
    Global::computeHistogram(&dst[(dstIdx - length) >> 1], length, histo);
    const int entropy = Global::computeFirstOrderEntropy1024(length, histo);

    if (entropy >= ent[0])
        return false;

    input._index = srcIdx;
    output._index = dstIdx;
    return true;
}

bool FSDCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("FSD codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("FSD codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count < 4)
        return false;

    const int srcEnd = count;
    const int dstEnd = output._length;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    // Retrieve mode & step value
    const byte mode = src[0];
    const int dist = int(src[1]);

    // Sanity check
    if ((dist < 1) || ((dist > 4) && (dist != 8)))
        return false;

    int srcIdx = 2;
    int dstIdx = 0;

    // Emit first bytes
    for (int i = 0; i < dist; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Recover original bytes
    if (mode == DELTA_CODING) {
        while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
            if (src[srcIdx] != ESCAPE_TOKEN) {
                const int delta = int(src[srcIdx] >> 1) ^ -int((src[srcIdx] & byte(1))); // zigzag decode delta
                dst[dstIdx] = byte(int(dst[dstIdx - dist]) + delta);
                srcIdx++;
                dstIdx++;
                continue;
            }

            srcIdx++;

            if (srcIdx == srcEnd)
                break;

            dst[dstIdx] = src[srcIdx] ^ dst[dstIdx - dist];
            srcIdx++;
            dstIdx++;
        }
    }
    else { // mode == XOR_CODING
        while (srcIdx < srcEnd) {
            dst[dstIdx] = src[srcIdx] ^ dst[dstIdx - dist];
            srcIdx++;
            dstIdx++;
        }
    }

    input._index = srcIdx;
    output._index = dstIdx;
    return srcIdx == srcEnd;
}
