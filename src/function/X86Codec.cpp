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

#include "X86Codec.hpp"

using namespace kanzi;

bool X86Codec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw std::invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw std::invalid_argument("Invalid output block");

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int end = count - 8;

    if (isExeBlock(src, end, count) == false)
       return false;
    
    int srcIdx = 0;
    int dstIdx = 0;

    while (srcIdx < end) {
        dst[dstIdx++] = src[srcIdx++];

        // Relative jump ?
        if ((src[srcIdx - 1] & MASK_JUMP) != INSTRUCTION_JUMP)
            continue;

        const byte cur = src[srcIdx];

        if ((cur == byte(0)) || (cur == byte(1)) || (cur == ESCAPE)) {
            // Conflict prevents encoding the address. Emit escape symbol
            dst[dstIdx] = ESCAPE;
            dst[dstIdx + 1] = cur;
            srcIdx++;
            dstIdx += 2;
            continue;
        }

        const int sgn = int(src[srcIdx + 3]);

        // Invalid sign of jump address difference => false positive ?
        if ((sgn != 0) && (sgn != 0xFF))
            continue;

        const int addr = ((0xFF & int(src[srcIdx])) | 
                         ((0xFF & int(src[srcIdx + 1])) << 8) | 
                         ((0xFF & int(src[srcIdx + 2])) << 16) | 
                          (sgn << 24)) + srcIdx;

        dst[dstIdx] = byte(sgn + 1);
        dst[dstIdx + 1] = MASK_ADDRESS ^ byte(0xFF & (addr >> 16));
        dst[dstIdx + 2] = MASK_ADDRESS ^ byte(0xFF & (addr >> 8));
        dst[dstIdx + 3] = MASK_ADDRESS ^ byte(0xFF & addr);
        srcIdx += 4;
        dstIdx += 4;
    }

    while (srcIdx < count)
        dst[dstIdx++] = src[srcIdx++];

    input._index = srcIdx;
    output._index = dstIdx;
    return true;
}

bool X86Codec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count) THROW
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw std::invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw std::invalid_argument("Invalid output block");

    byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int end = count - 8;
    int srcIdx = 0;
    int dstIdx = 0;

    while (srcIdx < end) {
        dst[dstIdx++] = src[srcIdx++];

        // Relative jump ?
        if ((src[srcIdx - 1] & MASK_JUMP) != INSTRUCTION_JUMP)
            continue;

        if (src[srcIdx] == ESCAPE) {
            // Not an encoded address. Skip escape symbol
            srcIdx++;
            continue;
        }

        const int sgn = int(src[srcIdx]) - 1;

        // Invalid sign of jump address difference => false positive ?
        if ((sgn != 0) && (sgn != -1))
            continue;

        const int addr = (((0xFF & sgn) << 24) |
                          ((0xFF & int(MASK_ADDRESS ^ src[srcIdx+1])) << 16) |
                          ((0xFF & int(MASK_ADDRESS ^ src[srcIdx+2])) <<  8) |
                           (0xFF & int(MASK_ADDRESS ^ src[srcIdx+3]))) - dstIdx;

        dst[dstIdx] = byte(addr);
        dst[dstIdx + 1] = byte(addr >> 8);
        dst[dstIdx + 2] = byte(addr >> 16);
        dst[dstIdx + 3] = byte(sgn);
        srcIdx += 4;
        dstIdx += 4;
    }

    while (srcIdx < count)
        dst[dstIdx++] = src[srcIdx++];

    input._index = srcIdx;
    output._index = dstIdx;
    return true;
}

int X86Codec::getMaxEncodedLength(int srcLen) const 
{ 
    // Since we do not check the dst index for each byte (for speed purpose)
    // allocate some extra buffer for incompressible data.
    if (srcLen >= 1 << 30)
        return srcLen;
      
    return (srcLen <= 512) ? srcLen + 32 : srcLen + srcLen / 16;
}


bool X86Codec::isExeBlock(byte src[], int end, int count) const 
{
    int jumps = 0;

    for (int i = 0; i < end; i++) {
        if ((src[i] & MASK_JUMP) == INSTRUCTION_JUMP) {
           if ((src[i+4] == byte(0)) || (src[i+4] == byte(0xFF))) {
              // Count relative jumps (E8/E9 .. .. .. 00/FF)
              jumps++;
           }
        } else if ((src[i] == PREFIX_JCC) && ((src[i+1] & MASK_JCC) == INSTRUCTION_JCC)) {
           // Count relative conditional jumps (0x0F 0x8.)
           jumps++;
        }
    }

    // Number of jump instructions too small => either not a binary
    // or not worth the change => skip. Very crude filter obviously.
    // Also, binaries usually have a lot of 0x88..0x8C (MOV) instructions.
    return jumps >= (count >> 7);
}
