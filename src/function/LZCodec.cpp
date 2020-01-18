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
#include "LZCodec.hpp"

using namespace kanzi;

int LZCodec::emitLastLiterals(const byte src[], byte dst[], int litLen)
{
    int dstIdx = 1;

    if (litLen >= 7) {
        dst[0] = byte(7 << 5);
        dstIdx += emitLength(&dst[1], litLen - 7);
    }
    else {
        dst[0] = byte(litLen << 5);
    }

    memcpy(&dst[dstIdx], src, litLen);
    return dstIdx + litLen;
}

bool LZCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    if (input._array == output._array)
        return false;

    if (output._length < getMaxEncodedLength(count))
        return false;

    // If too small, skip
    if (count < MIN_LENGTH)
        return false;

    const int srcEnd = count - 8;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    int dstIdx = 0;

    if (_bufferSize == 0) {
        _bufferSize = 1 << HASH_LOG;
        delete[] _hashes;
        _hashes = new int32[_bufferSize];
    }

    memset(_hashes, 0, sizeof(int32) * _bufferSize);
    const int maxDist = (srcEnd < 4 * MAX_DISTANCE1) ? MAX_DISTANCE1 : MAX_DISTANCE2;
    dst[dstIdx++] = (maxDist == MAX_DISTANCE1) ? byte(0) : byte(1);
    int srcIdx = 0;
    int anchor = 0;

    while (srcIdx < srcEnd) {
        const int minRef = max(srcIdx - maxDist, 0);
        const int32 h = hash(&src[srcIdx]);
        const int ref = _hashes[h];
        int bestLen = 0;

        // Find a match
        if ((ref > minRef) && (sameInts(src, ref, srcIdx) == true)) {
            const int maxMatch = srcEnd - srcIdx;
            bestLen = 4;

            while ((bestLen + 4 < maxMatch) && (sameInts(src, ref + bestLen, srcIdx + bestLen) == true))
                bestLen += 4;

            while ((bestLen < maxMatch) && (src[ref + bestLen] == src[srcIdx + bestLen]))
                bestLen++;
        }

        // No good match ?
        if (bestLen < MIN_MATCH) {
            _hashes[h] = srcIdx;
            srcIdx++;
            continue;
        }

        // Emit token
        // Token: 3 bits litLen + 1 bit flag + 4 bits mLen
        // flag = if maxDist = (1<<17)-1, then highest bit of distance
        //        else 1 if dist needs 3 bytes (> 0xFFFF) and 0 otherwise
        const int mLen = bestLen - MIN_MATCH;
        const int dist = srcIdx - ref;
        const int token = ((dist > 0xFFFF) ? 0x10 : 0) | min(mLen, 0x0F);

        // Literals to process ?
        if (anchor == srcIdx) {
            dst[dstIdx++] = byte(token);
        }
        else {
            // Process match
            const int litLen = srcIdx - anchor;

            // Emit literal length
            if (litLen >= 7) {
                dst[dstIdx++] = byte((7 << 5) | token);
                dstIdx += emitLength(&dst[dstIdx], litLen - 7);
            }
            else {
                dst[dstIdx++] = byte((litLen << 5) | token);
            }

            // Emit literals
            emitLiterals(&src[anchor], &dst[dstIdx], litLen);
            dstIdx += litLen;
        }

        // Emit match length
        if (mLen >= 0x0F)
            dstIdx += emitLength(&dst[dstIdx], mLen - 0x0F);

        // Emit distance
        if ((maxDist == MAX_DISTANCE2) && (dist > 0xFFFF))
            dst[dstIdx++] = byte(dist >> 16);

        dst[dstIdx++] = byte(dist >> 8);
        dst[dstIdx++] = byte(dist);

        // Fill _hashes and update positions
        anchor = srcIdx + bestLen;
        _hashes[h] = srcIdx;
        srcIdx++;

        while (srcIdx < anchor) {
            _hashes[hash(&src[srcIdx])] = srcIdx;
            srcIdx++;
        }
    }

    // Emit last literals
    dstIdx += emitLastLiterals(&src[anchor], &dst[dstIdx], srcEnd + 8 - anchor);
    input._index = srcEnd + 8;
    output._index = dstIdx;
    return true;
}

bool LZCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    if (input._array == output._array)
        return false;

    const int srcEnd = count - 8;
    const int dstEnd = output._length - 8;
    byte* dst = &output._array[output._index];
    byte* src = &input._array[input._index];
    int dstIdx = 0;
    const int maxDist = (src[0] == byte(1)) ? MAX_DISTANCE2 : MAX_DISTANCE1;
    int srcIdx = 1;

    while (true) {
        const int token = int(src[srcIdx++]);

        if (token >= 32) {
            // Get literal length
            int litLen = token >> 5;

            if (litLen == 7) {
                while ((srcIdx < srcEnd) && (src[srcIdx] == byte(0xFF))) {
                    srcIdx++;
                    litLen += 0xFF;
                }

                if (srcIdx >= srcEnd + 8) {
                    input._index += srcIdx;
                    output._index += dstIdx;
                    return false;
                }

                litLen += int(src[srcIdx++]);
            }

            // Copy literals and exit ?
            if ((dstIdx + litLen > dstEnd) || (srcIdx + litLen > srcEnd)) {
                memcpy(&dst[dstIdx], &src[srcIdx], litLen);
                srcIdx += litLen;
                dstIdx += litLen;
                break;
            }

            // Emit literals
            emitLiterals(&src[srcIdx], &dst[dstIdx], litLen);
            srcIdx += litLen;
            dstIdx += litLen;
        }

        // Get match length
        int mLen = token & 0x0F;

        if (mLen == 15) {
            while ((srcIdx < srcEnd) && (src[srcIdx] == byte(0xFF))) {
                srcIdx++;
                mLen += 0xFF;
            }

            if (srcIdx < srcEnd)
                mLen += int(src[srcIdx++]);
        }

        mLen += MIN_MATCH;
        const int mEnd = dstIdx + mLen;

        // Sanity check
        if (mEnd > dstEnd + 8) {
            input._index += srcIdx;
            output._index += dstIdx;
            return false;
        }

        // Get distance
        int dist = (int(src[srcIdx]) << 8) | int(src[srcIdx + 1]);
        srcIdx += 2;

        if ((token & 0x10) != 0) {
            dist = (maxDist == MAX_DISTANCE1) ? dist + 65536 : (dist << 8) | int(src[srcIdx++]);
        }

        // Sanity check
        if ((dstIdx < dist) || (dist > maxDist)) {
            input._index += srcIdx;
            output._index += dstIdx;
            return false;
        }

        // Copy match
        if (dist > 8) {
            int ref = dstIdx - dist;

            do {
                // No overlap
                memcpy(&dst[dstIdx], &dst[ref], 8);
                ref += 8;
                dstIdx += 8;
            } while (dstIdx < mEnd);
        }
        else {
            const int ref = dstIdx - dist;

            for (int i = 0; i < mLen; i++)
                dst[dstIdx + i] = dst[ref + i];
        }

        dstIdx = mEnd;
    }

    output._index = dstIdx;
    input._index = srcIdx;
    return srcIdx == srcEnd + 8;
}
