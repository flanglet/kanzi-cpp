/*
Copyright 2011-2025 Frederic Langlet
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
#include "SRT.hpp"

using namespace kanzi;

bool SRT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int length)
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
       throw std::invalid_argument("SRT: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw std::invalid_argument("SRT: Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(length))
        return false;

    uint freqs[256] = { 0 };
    uint8 s2r[256] = { 0 };
    uint8 r2s[256] = { 0 };
    const byte* src = &input._array[input._index];

    // find first symbols and count occurrences
    for (int i = 0, b = 0; i < length;) {
        uint8 c = uint8(src[i]);
        int j = i + 1;

        while ((j < length) && (src[j] == byte(c)))
            j++;

        if (freqs[c] == 0) {
            r2s[b] = c;
            s2r[c] = uint8(b);
            b++;
        }

        freqs[c] += (j - i);
        i = j;
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
    byte* dst = &output._array[output._index];

    // encoding
    for (int i = 0; i < length;) {
        uint8 c = uint8(src[i]);
        int r = s2r[c];
        int p = buckets[c];
        dst[p] = byte(r);
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

        while ((i < length) && (src[i] == byte(c))) {
            dst[p] = byte(0);
            p++;
            i++;
        }

        buckets[c] = p;
    }

    input._index += length;
    output._index += length;
    return true;
}

bool SRT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int length)
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw std::invalid_argument("SRT: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw std::invalid_argument("SRT: Invalid output block");

    uint freqs[256] = { 0 };
    const int headerSize = decodeHeader(&input._array[input._index], freqs);
    input._index += headerSize;
    length -= headerSize;

    if (length > output._length - output._index)
        return false;

    const byte* src = &input._array[input._index];
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
    byte* dst = &output._array[output._index];

    // decoding
    for (int i = 0; i < length; i++) {
        dst[i] = byte(c);

        if (buckets[c] < bucketEnds[c]) {
            const uint8 r = uint8(src[buckets[c]]);
            buckets[c]++;

            if (r == 0)
                continue;

        if (r <= 8) {
            if (r >= 1) r2s[0] = r2s[1];
            if (r >= 2) r2s[1] = r2s[2];
            if (r >= 3) r2s[2] = r2s[3];
            if (r >= 4) r2s[3] = r2s[4];
            if (r >= 5) r2s[4] = r2s[5];
            if (r >= 6) r2s[5] = r2s[6];
            if (r >= 7) r2s[6] = r2s[7];
            if (r >= 8) r2s[7] = r2s[8];
        } else {
            memmove(&r2s[0], &r2s[1], size_t(r));
        }

            r2s[r] = c;
            c = r2s[0];
        }
        else {
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

int SRT::encodeHeader(const uint freqs[], byte dst[])
{
    int dstIdx = 0;

    for (int i = 0; i < 256; i++) {
        uint f = freqs[i];

        if (f >= 128) {
            dst[dstIdx++] = byte(0x80 | f);
            f >>= 7;

            if (f >= 128) {
                dst[dstIdx++] = byte(0x80 | f);
                f >>= 7;

                if (f >= 128) {
                    dst[dstIdx++] = byte(0x80 | f);
                    f >>= 7;

                    if (f >= 128) {
                        dst[dstIdx++] = byte(0x80 | f);
                        f >>= 7;
                    }
                }
            }
        }

        dst[dstIdx++] = byte(f);
    }

    return dstIdx;
}

int SRT::decodeHeader(const byte src[], uint freqs[])
{
    int srcIdx = 0;
    uint res = 0;

    for (int i = 0; i < 256; i++) {
        uint val = uint(src[srcIdx++]);
        res = val & 0x7F;

        if (val >= 128) {
            val = uint(src[srcIdx++]);
            res |= ((val & 0x7F) << 7);

            if (val >= 128) {
                val = uint(src[srcIdx++]);
                res |= ((val & 0x7F) << 14);

                if (val >= 128) {
                    val = uint(src[srcIdx++]);
                    res |= ((val & 0x7F) << 21);

                    if (val >= 128) {
                        val = uint(src[srcIdx++]);
                        res |= ((val & 0x7F) << 28);
                    }
                }
            }
        }

        freqs[i] = res;
    }

    return srcIdx;
}
