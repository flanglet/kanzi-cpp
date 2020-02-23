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

#include "FPAQDecoder.hpp"
#include "EntropyUtils.hpp"

using namespace kanzi;

FPAQDecoder::FPAQDecoder(InputBitStream& bitstream) THROW
    : _bitstream(bitstream),
      _sba(new byte[0], 0)
{
    _low = 0;
    _high = TOP;
    _current = 0;
    _initialized = false;
    _ctxIdx = 1;

    for (int i = 0; i < 256; i++)
        _probs[i] = PSCALE >> 1;
}

FPAQDecoder::~FPAQDecoder()
{
    dispose();
    delete[] _sba._array;
}

int FPAQDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count >= 1 << 30)
        throw invalid_argument("Invalid block size parameter (max is 1<<30)");

    int startChunk = blkptr;
    const int end = blkptr + count;
    int length = (count < 64) ? 64 : count;

    if (count >= 1 << 26) {
        // If the block is big (>=64MB), split the decoding to avoid allocating
        // too much memory.
        length = (count < (1 << 29)) ? count >> 3 : count >> 4;
    }

    // Split block into chunks, read bit array from bitstream and decode chunk
    while (startChunk < end) {
        const int chunkSize = min(length, end - startChunk);

        if (_sba._length < chunkSize + (chunkSize >> 3)) {
           const int length = chunkSize + (chunkSize >> 3);
           delete[] _sba._array;
            _sba._array = new byte[length];
            _sba._length = length;
        }

        const int szBytes = int(EntropyUtils::readVarInt(_bitstream));
        _current = _bitstream.readBits(56);
        _initialized = true;
        
        if (szBytes != 0)
            _bitstream.readBits(&_sba._array[0], 8 * szBytes);

        _sba._index = 0;
        const int endChunk = startChunk + chunkSize;

        for (int i = startChunk; i < endChunk; i++)
            block[i] = decodeByte();

        startChunk = endChunk;
    }

    return count;
}

byte FPAQDecoder::decodeByte()
{
    _ctxIdx = 1;
    
    return byte((decodeBit(int(_probs[_ctxIdx] >> 4)) << 7)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 6)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 5)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 4)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 3)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 2)
        | (decodeBit(int(_probs[_ctxIdx] >> 4)) << 1)
        | decodeBit(int(_probs[_ctxIdx] >> 4)));
}

void FPAQDecoder::initialize()
{
    if (_initialized == true)
        return;

    _current = _bitstream.readBits(56);
    _initialized = true;
}

