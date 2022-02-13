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

#include "FSDCodec.hpp"
#include "../Global.hpp"
#include "../Magic.hpp"

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

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType) _pCtx->getInt("dataType", Global::UNDEFINED);

        if ((dt != Global::UNDEFINED) && (dt != Global::MULTIMEDIA))
            return false;
    }

    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    const int srcEnd = count;
    const int dstEnd = output._length;
    const int count5 = count / 5;
    const int count10 = count / 10;
    byte* in;
    uint histo[6][256];
    memset(&histo[0][0], 0, sizeof(uint) * 6 * 256);
    uint magic = Magic::getType(src);

    // Skip detection except for a few candidate types
    switch (magic) {
        case Magic::BMP_MAGIC:
        case Magic::RIFF_MAGIC:
        case Magic::PBM_MAGIC:
        case Magic::PGM_MAGIC:
        case Magic::PPM_MAGIC:
        case Magic::NO_MAGIC:
           break;
        default:
           return false;
    };

    // Check several step values on a sub-block (no memory allocation)
    // Sample 2 sub-blocks
    in = &src[count5 * 3];

    for (int i = 0; i < count10; i++) {
        const byte b = in[i];
        histo[0][int(b)]++;
        histo[1][int(b ^ in[i - 1])]++;
        histo[2][int(b ^ in[i - 2])]++;
        histo[3][int(b ^ in[i - 3])]++;
        histo[4][int(b ^ in[i - 4])]++;
        histo[5][int(b ^ in[i - 8])]++;
    }

    in = &src[count5 * 1];

    for (int i = count10; i < count5; i++) {
        const byte b = in[i];
        histo[0][int(b)]++;
        histo[1][int(b ^ in[i - 1])]++;
        histo[2][int(b ^ in[i - 2])]++;
        histo[3][int(b ^ in[i - 3])]++;
        histo[4][int(b ^ in[i - 4])]++;
        histo[5][int(b ^ in[i - 8])]++;
    }

    // Find if entropy is lower post transform
    int minIdx = 0;
    int ent[6];
    ent[0] = Global::computeFirstOrderEntropy1024(count5, histo[0]);

    for (int i = 1; i < 6; i++) {
        ent[i] = Global::computeFirstOrderEntropy1024(count5, histo[i]);

        if (ent[i] < ent[minIdx])
            minIdx = i;
    }

    // If not better, quick exit
    if (ent[minIdx] >= ent[0])
        return false;

    if (_pCtx != nullptr)
       _pCtx->putInt("dataType", Global::MULTIMEDIA);

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
                dst[dstIdx++] = byte(ZIGZAG[delta + 127]); // zigzag encode delta
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
    bool isFast = (_pCtx == nullptr) ? true : _pCtx->getInt("fullFSD") == 0;
    const int length = (isFast == true) ? dstIdx >> 1 : dstIdx;
    memset(histo[0], 0, sizeof(histo[0]));
    Global::computeHistogram(&dst[(dstIdx - length) >> 1], length, histo[0]);
    const int entropy = Global::computeFirstOrderEntropy1024(length, histo[0]);

    if (entropy >= ent[0])
        return false;

    input._index = srcIdx;
    output._index = dstIdx;
    return true; // Allowed to expand
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
