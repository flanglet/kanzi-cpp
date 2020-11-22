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

    // If not 'better enough', quick exit
    if ((_isFast == true) && (ent[minIdx] >= ((123 * ent[0]) >> 7)))
        return false;

    const int dist = (minIdx <= 4) ? minIdx : 8;
    int largeDeltas = 0;

    // Detect best coding by sampling for large deltas
    for (int i = 2 * count5; i < 3 * count5; i++) {
        const int32 delta = int32(src[i]) - int32(src[i - dist]);

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
            const int32 delta = int32(src[srcIdx]) - int32(src[srcIdx - dist]);

            if ((delta >= -127) && (delta <= 127)) {
                dst[dstIdx++] = byte((delta >> 31) ^ (delta << 1)); // zigzag encode delta
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
                const int32 delta = int32(src[srcIdx] >> 1) ^ -int32((src[srcIdx] & byte(1))); // zigzag decode delta
                dst[dstIdx] = byte(int32(dst[dstIdx - dist]) + delta);
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
