/*
Copyright 2011-2026 Frederic Langlet
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

#include <cstring>
#include <stdexcept>
#include "../Global.hpp"
#include "../Memory.hpp"
#include "SRT.hpp"

using namespace kanzi;

bool SRT::forward(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int length)
{
    if (length == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
       throw std::invalid_argument("SRT: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw std::invalid_argument("SRT: Invalid output block");

    if ((length < 0) || (output._length - output._index - MAX_HEADER_SIZE < length))
        return false;

    uint freqs[256] = { 0 };
    uint8 s2r[256] = { 0 };
    uint8 r2s[256] = { 0 };
    const kanzi::byte* src = &input._array[input._index];

    // Count symbol frequencies and recover first-appearance order for initial MTF list
    Global::computeHistogram(src, length, freqs, true, false);
    int firstPos[256];
    uint8 order[256];
    int nbOrder = 0;

    for (int c = 0; c < 256; c++) {
        if (freqs[c] == 0)
            continue;

        const void* p = memchr(src, c, size_t(length));
        firstPos[c] = int(static_cast<const kanzi::byte*>(p) - src);
        order[nbOrder++] = uint8(c);
    }

    for (int i = 1; i < nbOrder; i++) {
        const uint8 c = order[i];
        const int pos = firstPos[c];
        int j = i - 1;

        while ((j >= 0) && (firstPos[order[j]] > pos)) {
            order[j + 1] = order[j];
            j--;
        }

        order[j + 1] = c;
    }

    for (int i = 0; i < nbOrder; i++) {
        r2s[i] = order[i];
        s2r[order[i]] = uint8(i);
    }

    // init arrays
    uint8 symbols[256];
    int buckets[256] = { 0 };

    const int nbSymbols = preprocess(freqs, symbols);

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const uint8 c = symbols[i];
        buckets[c] = bucketPos;
        bucketPos += freqs[c];
    }

    const int headerSize = encodeHeader(freqs, &output._array[output._index]);
    output._index += headerSize;
    kanzi::byte* dst = &output._array[output._index];

    // encoding
    for (int i = 0; i < length;) {
        const uint8 c = uint8(src[i]);
        int r = s2r[c];
        int p = buckets[c];
        dst[p] = kanzi::byte(r);
        p++;

        if (r != 0) {
            do {
                const uint8 t = r2s[r - 1];
                r2s[r] = t;
                s2r[t] = uint8(r);
                r--;
            } while (r != 0);

            r2s[0] = c;
            s2r[c] = 0;
        }

        i++;

        // Emit runs of identical bytes as zeros in bulk using 64-bit word scan
        if ((i < length) && (src[i] == kanzi::byte(c))) {
            int j = i + 1;
            const uint64 c8 = uint64(c) * 0x0101010101010101ULL;

            while (j + 8 <= length) {
                const uint64 w = uint64(LittleEndian::readLong64(&src[j])) ^ c8;

                if (w != 0) {
                    j += Global::trailingZeros(w) >> 3;
                    break;
                }

                j += 8;
            }

            while ((j < length) && (src[j] == kanzi::byte(c)))
                j++;

            const int run = j - i;

            if (run <= 4) {
                for (int k = 0; k < run; k++)
                    dst[p + k] = kanzi::byte(0);
            }
            else {
                memset(&dst[p], 0, size_t(run));
            }

            p += run;
            i = j;
        }

        buckets[c] = p;
    }

    input._index += length;
    output._index += length;
    return true;
}

bool SRT::inverse(SliceArray<kanzi::byte>& input, SliceArray<kanzi::byte>& output, int length)
{
    if (length == 0)
        return true;

    if (!SliceArray<kanzi::byte>::isValid(input))
        throw std::invalid_argument("SRT: Invalid input block");

    if (!SliceArray<kanzi::byte>::isValid(output))
        throw std::invalid_argument("SRT: Invalid output block");

    if ((length < 256) || (input._index + length > input._length))
        return false;

    uint freqs[256] = { 0 };
    const int headerSize = decodeHeader(&input._array[input._index], length, freqs);

    if (headerSize < 0)
        return false;

    const int dataLength = length - headerSize;

    // Empty input is represented by a zero-length transform block, not a header-only block.
    if (dataLength <= 0)
        return false;

    // The frequency table must describe exactly the data following the header.
    // Otherwise a bucket end can point past the input block and the decoding
    // loop below can read outside the transform payload.
    uint64 totalFreq = 0;

    for (int i = 0; i < 256; i++)
        totalFreq += freqs[i];

    if (totalFreq != uint64(dataLength))
        return false;

    input._index += headerSize;
    length = dataLength;

    if (length > output._length - output._index)
        return false;

    const kanzi::byte* src = &input._array[input._index];
    uint8 symbols[256] = { 0 };

    // init arrays
    int nbSymbols = preprocess(freqs, symbols);
    int buckets[256] = { 0 };
    int bucketEnds[256] = { 0 };
    uint8 r2s[256] = { 0 };

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const uint8 c = symbols[i];

        if ((bucketPos < 0) || (bucketPos >= length))
            return false;

        r2s[int(src[bucketPos])] = c;
        buckets[c] = bucketPos + 1;
        bucketPos += freqs[c];
        bucketEnds[c] = bucketPos;
    }

    uint8 c = r2s[0];
    kanzi::byte* dst = &output._array[output._index];

    // decoding
    int i = 0;

    while (i < length) {
        int pos = buckets[c];
        const int end = bucketEnds[c];

        if (pos < end) {
            if (src[pos] == kanzi::byte(0)) {
                // Scan consecutive zeros in bucket c and emit runs of c in bulk
                int run = 1;

                if ((pos + 1 < end) && (src[pos + 1] == kanzi::byte(0))) {
                    run = 2;

                    while (pos + run + 8 <= end) {
                        const uint64 w = uint64(LittleEndian::readLong64(&src[pos + run]));

                        if (w != 0) {
                            run += Global::trailingZeros(w) >> 3;
                            break;
                        }

                        run += 8;
                    }

                    while ((pos + run < end) && (src[pos + run] == kanzi::byte(0)))
                        run++;
                }

                if ((run <= 8) && (i + 8 <= length)) {
                    const uint64 c8 = uint64(c) * 0x0101010101010101ULL;
                    memcpy(&dst[i], &c8, 8);
                }
                else {
                    memset(&dst[i], c, size_t(run));
                }

                i += run;
                pos += run;
                buckets[c] = pos;

                if (pos >= end) {
                    dst[i++] = kanzi::byte(c);

                    if (nbSymbols == 1)
                        continue;

                    nbSymbols--;
                    memmove(&r2s[0], &r2s[1], size_t(nbSymbols));
                    c = r2s[0];
                    continue;
                }
            }

            dst[i++] = kanzi::byte(c);
            const uint8 r = uint8(src[pos]);
            buckets[c] = pos + 1;

            if (r <= 8) {
                if (r >= 1) r2s[0] = r2s[1];
                if (r >= 2) r2s[1] = r2s[2];
                if (r >= 3) r2s[2] = r2s[3];
                if (r >= 4) r2s[3] = r2s[4];
                if (r >= 5) r2s[4] = r2s[5];
                if (r >= 6) r2s[5] = r2s[6];
                if (r >= 7) r2s[6] = r2s[7];
                if (r >= 8) r2s[7] = r2s[8];
            }
            else {
                memmove(&r2s[0], &r2s[1], size_t(r));
            }

            r2s[r] = c;
            c = r2s[0];
        }
        else {
            dst[i++] = kanzi::byte(c);

            if (nbSymbols == 1)
                continue;

            nbSymbols--;
            memmove(&r2s[0], &r2s[1], size_t(nbSymbols));
            c = r2s[0];
        }
    }

    input._index += length;
    output._index += length;
    return true;
}

int SRT::preprocess(const uint freqs[], uint8 symbols[])
{
    int nbSymbols = 0;

    for (int i = 0; i < 256; i++) {
        if (freqs[i] == 0)
            continue;

        symbols[nbSymbols] = uint8(i);
        nbSymbols++;
    }

    int h = 4;

    while (h < nbSymbols)
        h = h * 3 + 1;

    do {
        h /= 3;

        for (int i = h; i < nbSymbols; i++) {
            uint8 t = symbols[i];
            int b;

            for (b = i - h; b >= 0; b -= h) {
                const int val = freqs[symbols[b]] - freqs[t];

                if ((val >= 0) && ((val != 0) || (t >= symbols[b])))
                   break;

                symbols[b + h] = symbols[b];
            }

            symbols[b + h] = t;
        }
    } while (h != 1);

    return nbSymbols;
}

int SRT::encodeHeader(const uint freqs[], kanzi::byte dst[])
{
    int dstIdx = 0;

    for (int i = 0; i < 256; i++) {
        uint f = freqs[i];

        if (f >= 128) {
            dst[dstIdx++] = kanzi::byte(0x80 | f);
            f >>= 7;

            if (f >= 128) {
                dst[dstIdx++] = kanzi::byte(0x80 | f);
                f >>= 7;

                if (f >= 128) {
                    dst[dstIdx++] = kanzi::byte(0x80 | f);
                    f >>= 7;

                    if (f >= 128) {
                        dst[dstIdx++] = kanzi::byte(0x80 | f);
                        f >>= 7;
                    }
                }
            }
        }

        dst[dstIdx++] = kanzi::byte(f);
    }

    return dstIdx;
}

int SRT::decodeHeader(const kanzi::byte src[], int srcEnd, uint freqs[])
{
    int srcIdx = 0;

    for (int i = 0; i < 256; i++) {
        uint res = 0;
        int shift = 0;

        // Frequencies are encoded as varints with up to 5 bytes.
        for (int j = 0; j < 5; j++) {
            if (srcIdx >= srcEnd)
                return -1;

            const uint val = uint(src[srcIdx++]);
            res |= ((val & 0x7F) << shift);

            if ((val & 0x80) == 0)
                break;

            if (j == 4)
                return -1;

            shift += 7;
        }

        freqs[i] = res;
    }

    return srcIdx;
}
