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

#include <sstream>
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
    byte* dst1 = &dst[0];
    byte* dst2 = &dst[count5 * 1];
    byte* dst3 = &dst[count5 * 2];
    byte* dst4 = &dst[count5 * 3];
    byte* dst8 = &dst[count5 * 4];

    // Check several step values on a sub-block (no memory allocation)
    for (int i = 8; i < count5; i++) {
        const byte b = src[i];
        dst1[i] = b ^ src[i - 1];
        dst2[i] = b ^ src[i - 2];
        dst3[i] = b ^ src[i - 3];
        dst4[i] = b ^ src[i - 4];
        dst8[i] = b ^ src[i - 8];
    }

    // Find if entropy is lower post transform
    uint histo[256];
    int ent[6];
    ent[0] = Global::computeFirstOrderEntropy1024(&src[8], count5 - 8, histo);
    ent[1] = Global::computeFirstOrderEntropy1024(&dst1[8], count5 - 8, histo);
    ent[2] = Global::computeFirstOrderEntropy1024(&dst2[8], count5 - 8, histo);
    ent[3] = Global::computeFirstOrderEntropy1024(&dst3[8], count5 - 8, histo);
    ent[4] = Global::computeFirstOrderEntropy1024(&dst4[8], count5 - 8, histo);
    ent[5] = Global::computeFirstOrderEntropy1024(&dst8[8], count5 - 8, histo);

    int minIdx = 0;

    for (int i = 1; i < 6; i++) {
        if (ent[i] < ent[minIdx])
            minIdx = i;
    }

    // If not 'better enough', skip
    if (ent[minIdx] >= (96 * ent[0] / 100))
        return false;

    // Emit step value
    const int dist = (minIdx <= 4) ? minIdx : 8;
    dst[0] = byte(dist);
    int srcIdx = 0;
    int dstIdx = 1;

    // Emit first bytes
    for (int i = 0; i < dist; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Emit modified bytes
    while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
        const int32 delta = int32(src[srcIdx]) - int32(src[srcIdx - dist]);

        if ((delta >= -127) && (delta <= 127)) {
            dst[dstIdx++] = byte((delta >> 31) ^ (delta << 1)); // zigzag encode
            srcIdx++;
            continue;
        }

        if (dstIdx == dstEnd - 1)
            break;

        // Skip delta, direct encode
        dst[dstIdx++] = ESCAPE_TOKEN;
        dst[dstIdx++] = src[srcIdx];
        srcIdx++;
    }

    input._index = srcIdx;
    output._index = dstIdx;
    return srcIdx == srcEnd;
}

bool FSDCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("LZP codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("LZP codec: Invalid output block");

    if (input._array == output._array)
        return false;

    if (count < 4)
        return false;

    const int srcEnd = count;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];

    // Retrieve step value
    const int dist = int(src[0]);

    // Sanity check
    if ((dist < 1) || ((dist > 4) && (dist != 8)))
        return false;

    int srcIdx = 1;
    int dstIdx = 0;

    // Emit first bytes
    for (int i = 0; i < dist; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Invert bytes
    while (srcIdx < srcEnd) {
        if (src[srcIdx] == ESCAPE_TOKEN) {
            if (srcIdx == srcEnd - 1)
                break;

            dst[dstIdx++] = src[srcIdx + 1];
            srcIdx += 2;
            continue;
        }

        const int32 diff = int32(src[srcIdx] >> 1) ^ -int32((src[srcIdx] & byte(1))); // zigzag decode
        dst[dstIdx] = byte(int32(dst[dstIdx - dist]) + diff);
        srcIdx++;
        dstIdx++;
    }

    input._index = srcIdx;
    output._index = dstIdx;
    return srcIdx == srcEnd;
}
