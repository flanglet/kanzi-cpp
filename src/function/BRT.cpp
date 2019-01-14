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

#include "BRT.hpp"
#include "../IllegalArgumentException.hpp"

using namespace kanzi;

bool BRT::forward(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
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
    int32 ranks[256] = { 0 };
    int32 buckets[256] = { 0 };
    uint8 sortedMap[256] = { 0 };

    for (int i = 0; i < 256; i++)
        ranks[i] = i;

    int nbSymbols = computeFrequencies(input, length, freqs, ranks);  
    const int idx = encodeHeader(&output._array[output._index], freqs, nbSymbols);

    if (idx < 0)
        return false;

    sortMap(freqs, sortedMap);

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const int val = sortedMap[i];
        buckets[val] = bucketPos;
        bucketPos += freqs[val];
    }

    output._index += idx;
    byte* dst = &output._array[output._index];
    uint8* src = (uint8*)&input._array[input._index];

    for (int i = 0; i < length; i++) {
        const int s = src[i];
        const int32 r = ranks[s];
        dst[buckets[s]] = byte(r);
        buckets[s]++;

        if (r != 0) {
            for (int j = 0; j < 256; j += 8) {
                ranks[j] -= ((ranks[j] - r) >> 31);
                ranks[j + 1] -= ((ranks[j + 1] - r) >> 31);
                ranks[j + 2] -= ((ranks[j + 2] - r) >> 31);
                ranks[j + 3] -= ((ranks[j + 3] - r) >> 31);
                ranks[j + 4] -= ((ranks[j + 4] - r) >> 31);
                ranks[j + 5] -= ((ranks[j + 5] - r) >> 31);
                ranks[j + 6] -= ((ranks[j + 6] - r) >> 31);
                ranks[j + 7] -= ((ranks[j + 7] - r) >> 31);
            }

            ranks[s] = 0;
        }
    }

    input._index += length;
    output._index += length;
    return true;
}

int BRT::computeFrequencies(SliceArray<byte>& input, int length, int32 freqs[], int32 ranks[]) {
    int nbSymbols = 0;
    uint8* src = (uint8*)&input._array[input._index];   
    int n = 0;

    // Slow loop
    for (; n < length; n++) {
        const int s = src[n];

        if (freqs[s] == 0) {
            ranks[s] = nbSymbols;
            nbSymbols++;

            if (nbSymbols == 256)
                break;
        }

        freqs[s]++;
    }

    // Fast loop
    for (; n < length; n++)
        freqs[src[n]]++;

    return nbSymbols;
}

bool BRT::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int length) THROW
{
    if (length == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw IllegalArgumentException("Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw IllegalArgumentException("Invalid output block");

    int32 freqs[256] = { 0 };
    int32 ranks[256] = { 0 };
    int32 buckets[256] = { 0 };
    int32 bucketEnds[256] = { 0 };
    uint8 sortedMap[256] = { 0 };
    int total = 0;
    int nbSymbols = 0;

    const int idx = decodeHeader(&input._array[input._index], freqs, nbSymbols, total);

    if (idx < 0)
        return false;

    input._index += idx;

    if (total + idx != length)
        return false;

    if (nbSymbols == 0)
        return true;

    sortMap(freqs, sortedMap);
    uint8* src = (uint8*)&input._array[input._index];

    for (int i = 0, bucketPos = 0; i < nbSymbols; i++) {
        const int s = sortedMap[i];
        ranks[src[bucketPos]] = s;
        buckets[s] = bucketPos + 1;
        bucketPos += freqs[s];
        bucketEnds[s] = bucketPos;
    }

    int s = ranks[0];
    const int count = length - idx;
    byte* dst = &output._array[output._index];

    for (int i = 0; i < count; i++) {
        dst[i] = byte(s);
        int r = 0xFF;

        if (buckets[s] < bucketEnds[s]) {
            r = src[buckets[s]];
            buckets[s]++;

            if (r == 0)
               continue;
        }

        ranks[0] = ranks[1];

        for (int j = 1; j < r; j++)
            ranks[j] = ranks[j + 1];

        ranks[r] = s;
        s = ranks[0];
    }

    input._index += count;
    output._index += count;
    return true;
}

void BRT::sortMap(int32 freqs[], uint8 map[])
{
    int32 newFreqs[256];
    memcpy(&newFreqs[0], &freqs[0], 256 * sizeof(int32));

    for (int j = 0; j < 256; j++) {
        int32 max = newFreqs[0];
        int bsym = 0;

        for (int i = 1; i < 256; i++) {
            if (newFreqs[i] <= max)
                continue;

            bsym = i;
            max = newFreqs[i];
        }

        if (max == 0)
            break;

        map[j] = uint8(bsym);
        newFreqs[bsym] = 0;
    }
}

// Varint encode the frequencies to block
int BRT::encodeHeader(byte block[], int32 freqs[], int nbSymbols)
{
    int blkptr = 0;
    block[blkptr++] = byte(nbSymbols - 1);

    for (int i = 0; i < 256; i++) {
        int f = freqs[i];

        while (f >= 0x80) {
            block[blkptr++] = byte(0x80 | (f & 0x7F));
            f >>= 7;
        }

        block[blkptr++] = byte(f);

        if (freqs[i] > 0) {
            nbSymbols--;

            if (nbSymbols == 0)
                break;
        }
    }

    return blkptr;
}

// Varint decode the frequencies from block
int BRT::decodeHeader(byte block[], int32 freqs[], int& nbSymbols, int& total)
{
    int blkptr = 0;
    uint8* data = (uint8*)&block[0];
    int symbs = 1 + int(data[blkptr++]);
    nbSymbols = symbs;
    total = 0;

    for (int i = 0; i < 256; i++) {
        int f = int(data[blkptr++]);
        int res = f & 0x7F;
        int shift = 7;

        while ((f >= 0x80) && (shift <= 28)) {
            f = int(data[blkptr++]);
            res |= ((f & 0x7F) << shift);
            shift += 7;
        }

        if ((freqs[i] == 0) && (res != 0)) {
            symbs--;

            if (symbs == 0) {
                freqs[i] = res;
                total += res;
                break;
            }
        }

        freqs[i] = res;
        total += res;
    }

    return blkptr;
}
