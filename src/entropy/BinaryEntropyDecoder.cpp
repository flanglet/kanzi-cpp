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

#include "BinaryEntropyDecoder.hpp"
#include "EntropyUtils.hpp"
#include "../IllegalArgumentException.hpp"

using namespace kanzi;

BinaryEntropyDecoder::BinaryEntropyDecoder(InputBitStream& bitstream, Predictor* predictor, bool deallocate) THROW
    : _bitstream(bitstream),
      _sba(new byte[0], 0)
{
    if (predictor == nullptr)
        throw IllegalArgumentException("Invalid null predictor parameter");

    _predictor = predictor;
    _low = 0;
    _high = TOP;
    _current = 0;
    _initialized = false;
    _deallocate = deallocate;
}

BinaryEntropyDecoder::~BinaryEntropyDecoder()
{
    dispose();
    delete[] _sba._array;

    if (_deallocate)
        delete _predictor;
}

int BinaryEntropyDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count >= 1 << 30)
        throw IllegalArgumentException("Invalid block size parameter (max is 1<<30)");

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
        const int chunkSize = startChunk + length < end ? length : end - startChunk;

        if (_sba._length<(chunkSize * 9) >> 3) {
            delete[] _sba._array;
            _sba._array = new byte[(chunkSize * 9) >> 3];
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

byte BinaryEntropyDecoder::decodeByte()
{
    return byte((decodeBit() << 7)
        | (decodeBit() << 6)
        | (decodeBit() << 5)
        | (decodeBit() << 4)
        | (decodeBit() << 3)
        | (decodeBit() << 2)
        | (decodeBit() << 1)
        | decodeBit());
}

void BinaryEntropyDecoder::initialize()
{
    if (_initialized == true)
        return;

    _current = _bitstream.readBits(56);
    _initialized = true;
}

void BinaryEntropyDecoder::dispose()
{
}