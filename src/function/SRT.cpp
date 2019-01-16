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

#include "SRT.hpp"
#include "../IllegalArgumentException.hpp"

using namespace kanzi;

bool SRT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(length))
        return false;

    int32 freqs[256] = { 0 };
    uint8 s2r[256] = { 0 };
    int32 r2s[256] = { 0 };
    uint8* src = (uint8*)&input._array[input._index];

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
    byte symbols[256];
    int32 buckets[256];

    int nbSymbols = preprocess(freqs, symbols);

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
            dst[p] = 0;
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
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    // init arrays
    int32 freqs[256];
    const int headerSize = decodeHeader(&input._array[input._index], freqs);
    input._index += headerSize;
    byte* src = &input._array[input._index];
    byte symbols[256];
    int nbSymbols = preprocess(freqs, symbols);
    int32 buckets[256];
    int32 bucketEnds[256];
    uint8 r2s[256];

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        uint8 c = uint8(symbols[i]);
        r2s[src[bucketPos]] = c;
        buckets[c] = bucketPos + 1;
        bucketPos += int(freqs[c]);
        bucketEnds[c] = bucketPos;
    }

    // decoding
    uint8 c = r2s[0];
    uint8* dst = (uint8*)&output._array[output._index];

    for (int i = 0; i < length; i++) {
        dst[i] = c;

        if (buckets[c] < bucketEnds[c]) {
            int r = src[buckets[c]] & 0xFF;
            buckets[c]++;

            if (r == 0)
                continue;

            for (int s = 0; s < r; s++)
                r2s[s] = r2s[s + 1];

            r2s[r] = c;
            c = r2s[0];
        }
        else {
            nbSymbols--;

            if (nbSymbols == 0)
                continue;

            for (int s = 0; s < nbSymbols; s++)
                r2s[s] = r2s[s + 1];

            c = r2s[0];
        }
    }

    input._index += length;
    output._index += length;
    return true;
}

int SRT::preprocess(int32 freqs[], byte symbols[])
{
    int nbSymbols = 0;

    for (int i = 0; i < 256; i++) {
        if (freqs[i] == 0)
            continue;

        symbols[nbSymbols] = byte(i);
        nbSymbols++;
    }

    int h = 4;

    while (h < nbSymbols)
        h = h * 3 + 1;

    do {
        h /= 3;

        for (int i = h; i < nbSymbols; i++) {
            int t = symbols[i];
            int b;

            for (b = i - h; (b >= 0) && ((freqs[symbols[b]] < freqs[t]) || ((freqs[t] == freqs[symbols[b]]) && (t < symbols[b]))); b -= h) {
                symbols[b + h] = symbols[b];
            }

            symbols[b + h] = t;
        }
    } while (h != 1);

    return nbSymbols;
}

int SRT::encodeHeader(int32 freqs[], byte dst[])
{
    for (int i = 0; i < 256; i++) {
        dst[4 * i] = byte(freqs[i] >> 24);
        dst[4 * i + 1] = byte(freqs[i] >> 16);
        dst[4 * i + 2] = byte(freqs[i] >> 8);
        dst[4 * i + 3] = byte(freqs[i]);
    }

    return HEADER_SIZE;
}

int SRT::decodeHeader(byte src[], int32 freqs[])
{
    for (int i = 0; i < 256; i++) {
        const uint32 f1 = uint32(src[4 * i]);
        const uint32 f2 = uint32(src[4 * i + 1]);
        const uint32 f3 = uint32(src[4 * i + 2]);
        const uint32 f4 = uint32(src[4 * i + 3]);
        freqs[i] = int32((f1 << 24) | (f2 << 16) | (f3 << 8) | f4);
    }

    return HEADER_SIZE;
}
