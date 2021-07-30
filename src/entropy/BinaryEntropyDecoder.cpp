/*
Copyright 2011-2021 Frederic Langlet
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

using namespace kanzi;
using namespace std;

BinaryEntropyDecoder::BinaryEntropyDecoder(InputBitStream& bitstream, Predictor* predictor, bool deallocate) THROW
    : _bitstream(bitstream),
      _sba(new byte[0], 0)
{
    if (predictor == nullptr)
        throw invalid_argument("Invalid null predictor parameter");

    _predictor = predictor;
    _low = 0;
    _high = TOP;
    _current = 0;
    _deallocate = deallocate;
}

BinaryEntropyDecoder::~BinaryEntropyDecoder()
{
    _dispose();
    delete[] _sba._array;

    if (_deallocate)
        delete _predictor;
}

int BinaryEntropyDecoder::decode(byte block[], uint blkptr, uint count)
{
    if (count >= MAX_BLOCK_SIZE)
        throw invalid_argument("Invalid block size parameter (max is 1<<30)");

    int startChunk = blkptr;
    const int end = blkptr + count;
    int length = (count < 64) ? 64 : count;

    if (count >= MAX_CHUNK_SIZE) {
        // If the block is big (>=64MB), split the decoding to avoid allocating
        // too much memory.
        length = (count < 8 * MAX_CHUNK_SIZE) ? count >> 3 : count >> 4;
    }

    // Split block into chunks, read bit array from bitstream and decode chunk
    while (startChunk < end) {
        const int chunkSize = min(length, end - startChunk);
        const int szBytes = int(EntropyUtils::readVarInt(_bitstream));
        _current = _bitstream.readBits(56);

        if (szBytes == 0)
           break;

        if (_sba._length < szBytes) {
            delete[] _sba._array;
            _sba._length = szBytes + (szBytes >> 3);
            _sba._array = new byte[_sba._length];
        }

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
    return byte((decodeBit(_predictor->get()) << 7)
        | (decodeBit(_predictor->get()) << 6)
        | (decodeBit(_predictor->get()) << 5)
        | (decodeBit(_predictor->get()) << 4)
        | (decodeBit(_predictor->get()) << 3)
        | (decodeBit(_predictor->get()) << 2)
        | (decodeBit(_predictor->get()) << 1)
        | decodeBit(_predictor->get()));
}