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

#include <cstring>
#include <stdexcept>
#include "SRT.hpp"

using namespace kanzi;

bool SRT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(length))
        return false;

    int32 freqs[256] = { 0 };
    uint8 s2r[256] = { 0 };
    int32 r2s[256] = { 0 };
    uint8* src = reinterpret_cast<uint8*>(&input._array[input._index]);

    // find first symbols and count occurrences
    for (int i = 0, b = 0; i < length;) {
        uint8 c = src[i];
        int j = i + 1;

        while ((j < length) && (src[j] == c))
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
    int32 buckets[256] = { 0 };

    const int nbSymbols = preprocess(freqs, symbols);

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const int c = symbols[i] & 0xFF;
        buckets[c] = bucketPos;
        bucketPos += freqs[c];
    }

    const int headerSize = encodeHeader(freqs, &output._array[output._index]);
    output._index += headerSize;
    byte* dst = &output._array[output._index];

    // encoding
    for (int i = 0; i < length;) {
        uint8 c = src[i];
        int r = s2r[c];
        int p = buckets[c];
        dst[p] = byte(r);
        p++;

        if (r != 0) {
            do {
                r2s[r] = r2s[r - 1];
                s2r[r2s[r]] = r;
                r--;
            } while (r != 0);

            r2s[0] = c;
            s2r[c] = 0;
        }

        int j = i + 1;

        while ((j < length) && (src[j] == c)) {
            dst[p] = byte(0);
            p++;
            j++;
        }

        buckets[c] = p;
        i = j;
    }

    input._index += length;
    output._index += length;
    return true;
}

bool SRT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Invalid output block");

    int32 freqs[256];
    const int headerSize = decodeHeader(&input._array[input._index], freqs);
    input._index += headerSize;
    length -= headerSize;
    uint8* src = reinterpret_cast<uint8*>(&input._array[input._index]);
    uint8 symbols[256];

    // init arrays
    int nbSymbols = preprocess(freqs, symbols);

    int32 buckets[256] = { 0 };
    int32 bucketEnds[256] = { 0 };
    uint8 r2s[256] = { 0 };

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const uint8 c = symbols[i];
        r2s[int(src[bucketPos]) & 0xFF] = c;
        buckets[c] = bucketPos + 1;
        bucketPos += freqs[c];
        bucketEnds[c] = bucketPos;
    }

    // decoding
    uint8 c = r2s[0];
    uint8* dst = reinterpret_cast<uint8*>(&output._array[output._index]);

    for (int i = 0; i < length; i++) {
        dst[i] = c;

        if (buckets[c] < bucketEnds[c]) {
            const uint8 r = src[buckets[c]];
            buckets[c]++;

            if (r == 0)
                continue;

            memmove(&r2s[0], &r2s[1], r);
            r2s[r] = c;
            c = r2s[0];
        }
        else {
            if (nbSymbols == 1)
                continue;
            
            nbSymbols--;
            memmove(&r2s[0], &r2s[1], nbSymbols);
            c = r2s[0];
        }
    }

    input._index += length;
    output._index += length;
    return true;
}

int SRT::preprocess(int32 freqs[], uint8 symbols[])
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

            for (b = i - h; (b >= 0) && ((freqs[symbols[b]] < freqs[t]) || 
              ((freqs[t] == freqs[symbols[b]]) && (t < (symbols[b])))); b -= h) {
                symbols[b + h] = symbols[b];
            }

            symbols[b + h] = t;
        }
    } while (h != 1);

    return nbSymbols;
}

int SRT::encodeHeader(int32 freqs[], byte dst[])
{
    for (int i = 0, j = 0; i < 256; i++, j += 4) {
        dst[j] = byte(freqs[i] >> 24);
        dst[j + 1] = byte(freqs[i] >> 16);
        dst[j + 2] = byte(freqs[i] >> 8);
        dst[j + 3] = byte(freqs[i]);
    }

    return HEADER_SIZE;
}

int SRT::decodeHeader(byte src[], int32 freqs[])
{
    for (int i = 0, j = 0; i < 1024; i += 4, j++) {
        const int f1 = int(src[i]) & 0xFF;
        const int f2 = int(src[i + 1]) & 0xFF;
        const int f3 = int(src[i + 2]) & 0xFF;
        const int f4 = int(src[i + 3]) & 0xFF;
        freqs[j] = int32((f1 << 24) | (f2 << 16) | (f3 << 8) | f4);
    }

    return HEADER_SIZE;
}
