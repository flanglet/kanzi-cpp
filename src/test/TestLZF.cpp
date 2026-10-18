/*
Copyright 2011-2026 Frederic Langlet
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

                http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <iostream>
#include <vector>
#include "../transform/LZCodec.hpp"

using namespace std;
using namespace kanzi;

#define CHECK(condition, message) do { if (!(condition)) { \
    cerr << "LZF: " << message << " at line " << __LINE__ << endl; return 1; } } while (0)

static uint32 nextRandom(uint32& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static int roundTrip(LZFCodec& codec, const vector<kanzi::byte>& data, bool mustCompress)
{
    const int size = int(data.size());
    const int offset = 3;
    CHECK(size > 0 && size < INT_MAX - offset, "invalid test input size");
    vector<kanzi::byte> source(size + offset);
    memcpy(&source[offset], &data[0], data.size());
    vector<kanzi::byte> encoded(codec.getMaxEncodedLength(size) + offset, kanzi::byte(0xA5));
    SliceArray<kanzi::byte> input(&source[0], int(source.size()), offset);
    SliceArray<kanzi::byte> output(&encoded[0], int(encoded.size()), offset);
    const bool accepted = codec.forward(input, output, size);

    if (!accepted) {
        CHECK(!mustCompress, "compressible input was rejected");
        CHECK(input._index == offset && output._index == offset, "failed forward moved indexes");
        return 0;
    }

    CHECK(input._index == size + offset, "forward input index");
    CHECK((int(encoded[offset + 4]) & 0xFE) == 0x08, "encoder minimum match");
    const int count = output._index - offset;
    vector<kanzi::byte> packed(count + offset + 4, kanzi::byte(0));
    memcpy(&packed[offset], &encoded[offset], size_t(count));
    vector<kanzi::byte> decoded(size + offset + 15 + 16, kanzi::byte(0xA5));
    SliceArray<kanzi::byte> compressed(&packed[0], int(packed.size()), offset);
    SliceArray<kanzi::byte> reverse(&decoded[0], size + offset + 15, offset);
    CHECK(codec.inverse(compressed, reverse, count), "inverse rejected encoder output");
    CHECK(compressed._index == count + offset && reverse._index == size + offset, "inverse indexes");
    CHECK(memcmp(&decoded[offset], &data[0], size_t(size)) == 0, "round-trip mismatch");

    for (int i = 0; i < offset; i++)
        CHECK(decoded[i] == kanzi::byte(0xA5), "write before output slice");

    for (int i = size + offset + 15; i < int(decoded.size()); i++)
        CHECK(decoded[i] == kanzi::byte(0xA5), "write past output capacity");

    return 0;
}

static void appendLength(vector<kanzi::byte>& packed, int length)
{
    if (length < 254) {
        packed.push_back(kanzi::byte(length));
    }
    else if (length < 65790) {
        packed.push_back(kanzi::byte(254));
        packed.push_back(kanzi::byte((length - 254) >> 8));
        packed.push_back(kanzi::byte(length - 254));
    }
    else {
        packed.push_back(kanzi::byte(255));
        packed.push_back(kanzi::byte((length - 255) >> 16));
        packed.push_back(kanzi::byte((length - 255) >> 8));
        packed.push_back(kanzi::byte(length - 255));
    }
}

// Independent current-format fixture: long literals, distance 8, a repeat,
// another explicit distance, then the required 18-byte literal tail.
static int wireFixture(int distance, int repeatLength, int minMatch)
{
    const int prefix = distance + 48;
    vector<kanzi::byte> packed(5, kanzi::byte(0));
    vector<kanzi::byte> expected;
    packed.push_back(kanzi::byte(0x3F));
    appendLength(packed, prefix - 48);
    uint32 random = 0x51327468;

    for (int i = 0; i < prefix; i++) {
        const kanzi::byte b = kanzi::byte(nextRandom(random));
        packed.push_back(b);
        expected.push_back(b);
    }

    const int firstMatch = int(packed.size());
    packed.push_back(kanzi::byte(0x40)); // Minimum length, distance 8, one byte.
    packed.push_back(kanzi::byte(0));
    const int extra = repeatLength - minMatch;
    packed.push_back(kanzi::byte((extra < 15) ? extra : 15));

    if (extra >= 15)
        appendLength(packed, extra - 15);

    for (int i = 0; i < minMatch + repeatLength; i++)
        expected.push_back(expected[int(expected.size()) - 8]);

    const uint32 value = uint32(distance - 8);
    const int width = (value < 1024) ? 1 : ((value < 262144) ? 2 : 3);
    packed.push_back(kanzi::byte((width << 6) | ((value & 3) << 4)));

    for (int i = 0; i < width; i++)
        packed.push_back(kanzi::byte(value >> (2 + 8 * i)));

    for (int i = 0; i < minMatch; i++)
        expected.push_back(expected[int(expected.size()) - distance]);

    packed.push_back(kanzi::byte(0x21)); // Final 18 literals.

    for (int i = 0; i < 18; i++) {
        const kanzi::byte b = kanzi::byte(80 + i);
        packed.push_back(b);
        expected.push_back(b);
    }

    const int count = int(packed.size());
    packed.resize(count + 4, kanzi::byte(0));
    LittleEndian::writeInt32(&packed[0], int(expected.size()));
    const int distanceFlag = distance > 65534;
    packed[4] = kanzi::byte(((minMatch - 2) << 1) | distanceFlag);
    vector<kanzi::byte> decoded(expected.size() + 15 + 16, kanzi::byte(0xA5));
    LZFCodec codec;
    SliceArray<kanzi::byte> input(&packed[0], int(packed.size()), 0);
    SliceArray<kanzi::byte> output(&decoded[0], int(expected.size()) + 15, 0);
    CHECK(codec.inverse(input, output, count), "wire fixture rejected");
    CHECK(input._index == count && output._index == int(expected.size()), "wire fixture indexes");
    CHECK(memcmp(&decoded[0], &expected[0], expected.size()) == 0, "wire fixture mismatch");

    for (int i = int(expected.size()) + 15; i < int(decoded.size()); i++)
        CHECK(decoded[i] == kanzi::byte(0xA5), "wire fixture output canary");

    for (int cut = count - 18; cut < count; cut++) {
        input._index = output._index = 0;
        CHECK(!codec.inverse(input, output, cut), "truncated wire fixture accepted");
        CHECK(input._index == 0 && output._index == 0, "truncation moved indexes");
    }

    input._index = output._index = 0;
    input._length = count + 3;
    CHECK(!codec.inverse(input, output, count), "missing input guard accepted");
    input._length = int(packed.size());
    output._length = int(expected.size()) + 14;
    CHECK(!codec.inverse(input, output, count), "insufficient output capacity accepted");
    CHECK(input._index == 0 && output._index == 0, "capacity failure moved indexes");
    output._length = int(expected.size()) + 15;

    packed[4] = kanzi::byte(0x20);
    CHECK(!codec.inverse(input, output, count), "reserved header bits accepted");
    packed[4] = kanzi::byte(((minMatch - 2) << 1) | distanceFlag);
    packed[firstMatch] = kanzi::byte(0); // Repeat without a prior match.
    CHECK(!codec.inverse(input, output, count), "initial repeat accepted");
    packed[firstMatch] = kanzi::byte(0x40);
    return 0;
}

#ifdef __GNUG__
int main()
#else
int TestLZF_main()
#endif
{
    LZFCodec codec;
    const int sizes[] = { 64, 255, 256, 512, 1024, 65790, 65793, 131584, 262144 };

    // After matching ABCDEF at distance 8, BCDEFX matches at distance 7,
    // but not at the repeat distance 8.
    vector<kanzi::byte> shifted(128);
    const char prefix[] = "ABCDEFGHABCDEFXYBCDEFX";
    memcpy(&shifted[0], prefix, sizeof(prefix) - 1);

    for (int i = int(sizeof(prefix) - 1); i < int(shifted.size()); i++)
        shifted[i] = kanzi::byte('Z');

    CHECK(roundTrip(codec, shifted, true) == 0, "repeat-distance verification");

    for (int period = 1; period <= 31; period++) {
        for (unsigned int j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            vector<kanzi::byte> data(sizes[j]);

            for (int i = 0; i < sizes[j]; i++)
                data[i] = kanzi::byte(i % period);

            CHECK(roundTrip(codec, data, sizes[j] >= 255) == 0, "periodic round trip");
        }
    }

    uint32 random = 0x10203040;

    for (int trial = 0; trial < 400; trial++) {
        const int size = 24 + int(nextRandom(random) % 32768);
        vector<kanzi::byte> data(size);

        for (int i = 0; i < size; i++) {
            const uint32 value = nextRandom(random);
            data[i] = (i >= 32 && (value & 7) != 0) ? data[i - 16] : kanzi::byte(value);
        }

        CHECK(roundTrip(codec, data, false) == 0, "mixed round trip");
    }

    const int distances[] = { 8, 9, 15, 16, 1031, 1032, 65534, 65535, 262151, 262152 };
    const int repeatLengths[] = { 6, 20, 21, 274, 275, 65793 };

    for (unsigned int i = 0; i < sizeof(distances) / sizeof(distances[0]); i++) {
        for (unsigned int j = 0; j < sizeof(repeatLengths) / sizeof(repeatLengths[0]); j++)
            CHECK(wireFixture(distances[i], repeatLengths[j], 6) == 0, "wire boundaries");
    }

    const int minMatches[] = { 2, 3, 4, 9, 17 };

    for (unsigned int i = 0; i < sizeof(minMatches) / sizeof(minMatches[0]); i++) {
        const int mm = minMatches[i];
        CHECK(wireFixture(16, mm, mm) == 0, "minimum match boundary");
        CHECK(wireFixture(16, mm + 14, mm) == 0, "inline match boundary");
        CHECK(wireFixture(16, mm + 15, mm) == 0, "extended match boundary");
    }

    for (int trial = 0; trial < 20000; trial++) {
        vector<kanzi::byte> packed(5, kanzi::byte(0));
        const int packets = 1 + int(nextRandom(random) % 4);
        int literalCount = 0;

        for (int i = 0; i < packets; i++) {
            const int length = 1 + int(nextRandom(random) % 47);
            packed.push_back(kanzi::byte(length + 15));

            for (int j = 0; j < length; j++)
                packed.push_back(kanzi::byte(nextRandom(random)));

            literalCount += length;
        }

        // A repeat match before any explicit match must fail after the
        // decoder has processed the randomized literal packets.
        packed.push_back(kanzi::byte(0));

        for (int i = 0; i < 18; i++)
            packed.push_back(kanzi::byte(nextRandom(random)));

        const int count = int(packed.size());
        packed.resize(count + 4, kanzi::byte(0));
        LittleEndian::writeInt32(&packed[0], 24 + literalCount);
        packed[4] = kanzi::byte(0x08 | (trial & 1));
        vector<kanzi::byte> decoded(256 + 15 + 16, kanzi::byte(0xA5));
        SliceArray<kanzi::byte> input(&packed[0], int(packed.size()), 0);
        SliceArray<kanzi::byte> output(&decoded[0], 256 + 15, 0);
        CHECK(!codec.inverse(input, output, count), "malformed stream accepted");
        CHECK(input._index == 0 && output._index == 0, "malformed stream moved indexes");

        for (int i = 256 + 15; i < int(decoded.size()); i++)
            CHECK(decoded[i] == kanzi::byte(0xA5), "malformed stream wrote past capacity");
    }

    cout << "LZF current-format round trips, wire boundaries and malformed streams passed" << endl;
    return 0;
}
