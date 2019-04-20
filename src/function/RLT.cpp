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

#include "RLT.hpp"
#include "../IllegalArgumentException.hpp"

using namespace kanzi;


bool RLT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(length))
        return false;

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    int srcIdx = 0;
    int dstIdx = 0;
    const int srcEnd = srcIdx + length;
    const int srcEnd4 = srcEnd - 4;
    const int dstEnd = output._length;
    uint freqs[256] = { 0 };
    Global::computeHistogram(&src[srcIdx], srcEnd, freqs, true, false);

    int minIdx = 0;

    if (freqs[minIdx] > 0) {
        for (int i=1; i<256; i++) {
            if (freqs[i] < freqs[minIdx]) {
                minIdx = i;

                if (freqs[i] == 0)
                    break;
            }
        }
    }

    bool res = true;
    byte escape = byte(minIdx);
    int run = 0;
    byte prev = src[srcIdx++];
    dst[dstIdx++] = escape;
    dst[dstIdx++] = prev;

    if (prev == escape)
        dst[dstIdx++] = byte(0);

    // Main loop
    while (srcIdx < srcEnd4) {
        if (prev == src[srcIdx]) {
            srcIdx++; run++;

            if (prev == src[srcIdx]) {
                srcIdx++; run++;

                if (prev == src[srcIdx]) {
                    srcIdx++; run++; ;

                    if (prev == src[srcIdx]) {
                        srcIdx++; run++;

                        if (run < MAX_RUN4)
                            continue;
                    }
                }
            }
        }

        if (run > RUN_THRESHOLD) {
            const int dIdx = emitRunLength(&dst[dstIdx], dstEnd-dstIdx, run, escape, prev);

            if (dIdx < 0) {
               res = false;
               break;
            }

            dstIdx += dIdx;
        }
        else if (prev != escape) {
            if (dstIdx+run >= dstEnd)
            {
               res = false;
               break;
            }

            if (run-- > 0)
               dst[dstIdx++] = prev;

            while (run-- > 0)
               dst[dstIdx++] = prev;
        }
        else { // escape literal
            if (dstIdx+2*run >= dstEnd)
            {
               res = false;
               break;
            }

            while (run-- > 0) {
               dst[dstIdx++] = escape;
               dst[dstIdx++] = byte(0);
            }
        }

        prev = src[srcIdx];
        srcIdx++;
        run = 1;
    }

    if (res == true) {
        // Process any remaining run
        if (run > RUN_THRESHOLD) {
            const int dIdx = emitRunLength(&dst[dstIdx], dstEnd-dstIdx, run, escape, prev);

            if (dIdx > 0)
               dstIdx += dIdx;
        }
        else if (prev != escape) {
            if (dstIdx + run < dstEnd) {
               while (run-- > 0)
                  dst[dstIdx++] = prev;
            }
        }
        else { // escape literal
            if (dstIdx + 2 * run < dstEnd) {
               while (run-- > 0) {
                  dst[dstIdx++] = escape;
                  dst[dstIdx++] = byte(0);
               }
            }
        }

        // Copy the last few bytes
        while ((srcIdx < srcEnd) && (dstIdx < dstEnd))
            dst[dstIdx++] = src[srcIdx++];

        res = srcIdx == srcEnd;
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return res;
}

int RLT::emitRunLength(byte* dst, int length, int run, byte escape, byte val) {
    int dstIdx = 1;
    dst[0] = val;

    if (val == escape) {
        dst[1] = byte(0);
        dstIdx = 2;
    }

    dst[dstIdx++] = escape;
    run -= RUN_THRESHOLD;

    // Encode run length
    if (run >= RUN_LEN_ENCODE1) {
        if (run < RUN_LEN_ENCODE2) {
            if (dstIdx >= length - 2)
                return -1;

            run -= RUN_LEN_ENCODE1;
            dst[dstIdx++] = byte(RUN_LEN_ENCODE1 + (run >> 8));
        }
        else {
            if (dstIdx >= length - 3)
                return -1;

            run -= RUN_LEN_ENCODE2;
            dst[dstIdx++] = byte(0xFF);
            dst[dstIdx++] = byte(run >> 8);
        }
    }

    dst[dstIdx] = byte(run);
    return dstIdx + 1;
}

bool RLT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    uint8* src = (uint8*)&input._array[input._index];
    uint8* dst = (uint8*)&output._array[output._index];
    int srcIdx = 0;
    int dstIdx = 0;
    const int srcEnd = srcIdx + length;
    const int dstEnd = output._length;
    bool res = true;
    uint8 escape = src[srcIdx++];

    if (src[srcIdx] == escape) {
        srcIdx++;

        // The data cannot start with a run but may start with an escape literal
        if ((srcIdx < srcEnd) && (src[srcIdx] != 0))
            return false;

        dst[dstIdx++] = escape;
        srcIdx++;
    }

    // Main loop
    while (srcIdx < srcEnd) {
        if (src[srcIdx] != escape) {
            // Literal
            if (dstIdx >= dstEnd) {
                  res = false;
                  break;
            }

            dst[dstIdx++] = src[srcIdx++];
            continue;
        }

        srcIdx++;

        if (srcIdx >= srcEnd) {
            res = false;
            break;
        }

        const uint8 val = dst[dstIdx-1];
        int run = src[srcIdx++];

        if (run == 0) {
            // Just an escape symbol, not a run
            if (dstIdx >= dstEnd) {
                  res = false;
                  break;
            }

            dst[dstIdx++] = escape;
            continue;
        }

        // Decode run length
        if (run == 0xFF) {
            if (srcIdx + 1 >= srcEnd) {
                  res = false;
                  break;
            }

            run = (uint8(src[srcIdx]) << 8) | uint8(src[srcIdx + 1]);
            srcIdx += 2;
            run += RUN_LEN_ENCODE2;
        }
        else if (run >= RUN_LEN_ENCODE1) {
            if (srcIdx >= srcEnd) {
                  res = false;
                  break;
            }

            run = ((run - RUN_LEN_ENCODE1) << 8) | uint8(src[srcIdx++]);
            run += RUN_LEN_ENCODE1;
        }

        run += (RUN_THRESHOLD - 1);

        if ((dstIdx + run >= dstEnd) || (run > MAX_RUN)) {
            res = false;
            break;
        }

        // Emit 'run' times the previous byte
        while (run >= 4) {
            dst[dstIdx] = val;
            dst[dstIdx + 1] = val;
            dst[dstIdx + 2] = val;
            dst[dstIdx + 3] = val;
            dstIdx += 4;
            run -= 4;
        }

        while (run > 0) {
            dst[dstIdx++] = val;
            run--;
        }
    }

    res &= srcIdx == srcEnd;
    input._index += srcIdx;
    output._index += dstIdx;
    return res;
}