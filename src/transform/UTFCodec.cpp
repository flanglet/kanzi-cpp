/*
Copyright 2011-2024 Frederic Langlet
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

#include <algorithm>
#include <cstring>
#include <vector>
#include "UTFCodec.hpp"
#include "../Global.hpp"
#include "../types.hpp"

using namespace kanzi;
using namespace std;

const int UTFCodec::MIN_BLOCK_SIZE = 1024;
const int UTFCodec::LEN_SEQ[256] = {
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
   3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
   4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

bool UTFCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < MIN_BLOCK_SIZE)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("UTFCodec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("UTFCodec: Invalid output block");

    if (output._length - output._index < getMaxEncodedLength(count))
        return false;

    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    int start = 0;
    const uint32 bom = 0xEFBBBF;

    if (memcmp(&src[start], &bom, 3) == 0) {
        // Byte Order Mark (BOM)
        start = 3;
    }
    else {
        // First (possibly) invalid symbols (due to block truncation).
        // It is possible to start in the middle of a file and end up
        // with invalid looking UTF data even if it is valid as a whole
        // (the current block may start in the middle of a symbol sequence).
        // A strict validation will reject the processing in this case.
        while ((start < 4) && (LEN_SEQ[uint8(src[start])] == 0))
            start++;
    }

    // 1-3 bit size + (7 or 11 or 16 or 21) bit payload
    // 3 MSBs indicate symbol size (limit map size to 22 bits)
    // 000 -> 7 bits
    // 001 -> 11 bits
    // 010 -> 16 bits
    // 1xx -> 21 bits
    uint32* aliasMap = new uint32[1 << 22];
    memset(aliasMap, 0, size_t(1 << 22) * sizeof(uint32));
    vector<sdUTF> v;
    v.reserve(max(count >> 10, 256));
    int n = 0;
    bool res = true;

    // Valid UTF-8
    // See Unicode 16 Standard - UTF-8 Table 3.7
    // U+0000..U+007F          00..7F
    // U+0080..U+07FF          C2..DF 80..BF
    // U+0800..U+0FFF          E0 A0..BF 80..BF
    // U+1000..U+CFFF          E1..EC 80..BF 80..BF
    // U+D000..U+D7FF          ED 80..9F 80..BF 80..BF
    // U+E000..U+FFFF          EE..EF 80..BF 80..BF
    // U+10000..U+3FFFF        F0 90..BF 80..BF 80..BF
    // U+40000..U+FFFFF        F1..F3 80..BF 80..BF 80..BF
    // U+100000..U+10FFFF      F4 80..8F 80..BF 80..BF
    for (int i = start; i < (count - 4); ) {
        uint32 val;
        const int s = pack(&src[i], val);

        if (s == 0) {
            res = false;
            break;
        }

        // Add to map ?
        if (aliasMap[val] == 0) {
            // Validate sequence
            const uint8 first = uint8(src[i]);

            switch( LEN_SEQ[first]) {
                case 1:
                   // First byte in [0x00..0x7F]
                   res &= (first < 0x80);
                   break;

                case 2:
                   // Second byte in [0x80..0xBF]
                   res &= !((src[i + 1] < byte(0x80) || (src[i + 1] > byte(0xBF))));
                   break;

                case 3:
                {
                   bool isSeq31 = first == 0xE0;
                   bool isSeq32 = (first >= 0xE1) && (first <= 0xEC);
                   bool isSeq33 = first == 0xED;
                   bool isSeq34 = (first == 0xEE) || (first == 0xEF);

                   // Combine second and third bytes
                   const uint16 val = uint16(src[i + 1]) << 8 | uint16(src[i + 2]);
                   // Third byte in [0x80..0xBF]
                   res &= !((src[i + 2] < byte(0x80)) || (src[i + 2] > byte(0xBF)));
                   // If first byte is 0xE0 then second byte in [0xA0..0xBF]
                   res &= (!isSeq31 || ((src[i + 1] >= byte(0xA0) && (src[i + 1] <= byte(0xBF)))));
                   // If first byte in [0xE1..0xEC] then second and third byte in [0x80..0xBF]
                   res &= (!isSeq32 || ((val & 0xC0C0) == 0x8080));
                   // If first byte is 0xED then second byte in [0x80..0x9F]
                   res &= (!isSeq33 || ((src[i + 1] >= byte(0x80)) && (src[i + 1] <= byte(0x9F))));
                   // If first byte in [0xEE..0xEF] then second and third byte in [0x80..0xBF]
                   res &= (!isSeq34 || ((val & 0xC0C0) == 0x8080));
                   break;
                }

                case 4:
                {
                   bool isSeq41 = first == 0xF0;
                   bool isSeq42 = (first >= 0xF1) && (first <= 0xF3);
                   bool isSeq43 = first == 0xF4;
                   bool secondByte80 = src[i + 1] < byte(0x80);
                   bool secondByteBF = src[i + 1] > byte(0xBF);

                   // Combine third and fourth bytes
                   const uint16 val = uint16(src[i + 2]) << 8 | uint16(src[i + 3]);
                   // Third and fourth bytes in [0x80..0xBF]
                   res &= ((val & 0xC0C0) == 0x8080);
                   // If first byte is is 0xF0 then second byte in [0x90..0x8F]
                   res &= !(isSeq41 && ((src[i + 1] < byte(0x90) || secondByteBF)));
                   // If first byte is in [0xF1..0xF3] then second byte in [0x80..0xBF]
                   res &= !(isSeq42 && (secondByte80 || secondByteBF));
                   // If first byte is 0xF4 then second byte in [0x80..0x8F]
                   res &= !(isSeq43 && (secondByte80 || (src[i + 1] > byte(0x8F))));
                   break;
                }

                default:
                   // Symbols that can never appear: 0xC0, 0xC1 and 0xF5..0xFF
                   res = false;
                   break;
            }

            n++;
            res &= (n < 32768);

            if (res == false)
               break;

#if __cplusplus >= 201103L
            v.emplace_back(val, 0);
#else
            sdUTF u(val, 0);
            v.push_back(u);
#endif
        }

        aliasMap[val]++;
        i += s;
    }

    const int maxTarget = count - (count / 10);

    if ((res == false) || (n == 0) || ((3 * n + 6) >= maxTarget)) {
        delete[] aliasMap;
        return false;
    }

    for (int i = 0; i < n; i++)
        v[i].freq = aliasMap[v[i].val];

    // Sort ranks by decreasing frequencies;
    sort(v.begin(), v.end());

    int dstIdx = 2;

    // Emit map length then map data
    dst[dstIdx++] = byte(n >> 8);
    dst[dstIdx++] = byte(n);

    int estimate = dstIdx + 6;

    for (int i = 0; i < n; i++) {
        estimate += int((i < 128) ? v[i].freq : 2 * v[i].freq);
        const uint32 s = v[i].val;
        aliasMap[s] = (i < 128) ? i : 0x10080 | ((i << 1) & 0xFF00) | (i & 0x7F);
        dst[dstIdx] = byte(s >> 16);
        dst[dstIdx + 1] = byte(s >> 8);
        dst[dstIdx + 2] = byte(s);
        dstIdx += 3;
    }

    if (estimate >= maxTarget) {
        // Not worth it
        delete[] aliasMap;
        return false;
    }

    // Emit first (possibly invalid) symbols (due to block truncation)
    for (int i = 0; i < start; i++)
        dst[dstIdx++] = src[i];

    v.clear();
    int srcIdx = start;

    // Emit aliases
    while (srcIdx < count - 4) {
        uint32 val;
        srcIdx += pack(&src[srcIdx], val);
        const uint32 alias = aliasMap[val];
        dst[dstIdx++] = byte(alias);
        dst[dstIdx] = byte(alias >> 8);
        dstIdx += (alias >> 16);
    }

    dst[0] = byte(start);
    dst[1] = byte(srcIdx - (count - 4));

    // Emit last (possibly invalid) symbols (due to block truncation)
    while (srcIdx < count)
        dst[dstIdx++] = src[srcIdx++];

    delete[] aliasMap;
    input._index += srcIdx;
    output._index += dstIdx;
    return dstIdx < maxTarget;
}

bool UTFCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < 4)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("UTFCodec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("UTFCodec: Invalid output block");

    const byte* src = &input._array[input._index];
    byte* dst = &output._array[output._index];
    const int start = int(src[0]) & 0x03;
    const int adjust = int(src[1]) & 0x03; // adjust end of regular processing
    const int n = (int(src[2]) << 8) + int(src[3]);

    // Protect against invalid map size value
    if ((n == 0) || (n >= 32768) || (3 * n >= count))
        return false;

    struct symb {
        uint32 val;
        uint8 len;
    };

    symb m[32768];
    int srcIdx = 4;

    // Build inverse mapping
    for (int i = 0; i < n; i++) {
        int s = (uint32(src[srcIdx]) << 16) | (uint32(src[srcIdx + 1]) << 8) | uint32(src[srcIdx + 2]);
        const int sl = unpack(s, (byte*)&m[i].val);

        if (sl == 0)
            return false;

        m[i].len = uint8(sl);
        srcIdx += 3;
    }

    int dstIdx = 0;
    const int srcEnd = count - 4 + adjust;
    const int dstEnd = output._length - 4;

    if (dstEnd < 0)
        return false;

    for (int i = 0; i < start; i++)
        dst[dstIdx++] = src[srcIdx++];

    // Emit data
    while ((srcIdx < srcEnd) && (dstIdx < dstEnd)) {
        uint alias = uint(src[srcIdx++]);

        if (alias >= 128)
            alias = (uint(src[srcIdx++]) << 7) + (alias & 0x7F);

        const symb& s = m[alias];
        memcpy(&dst[dstIdx], &s.val, 4);
        dstIdx += s.len;
    }

    if ((srcIdx >= srcEnd) && (dstIdx < dstEnd + adjust)) {
        for (int i = 0; i < 4 - adjust; i++)
            dst[dstIdx++] = src[srcIdx++];
    }

    input._index += srcIdx;
    output._index += dstIdx;
    return srcIdx == count;
}

