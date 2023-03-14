/*
Copyright 2011-2022 Frederic Langlet
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
#include <vector>
#include "AliasCodec.hpp"
#include "../Global.hpp"

using namespace kanzi;
using namespace std;


bool AliasCodec::forward(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (count < MIN_BLOCK_SIZE)
        return false;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Alias codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Alias codec: Invalid output block");

    if (output._length < getMaxEncodedLength(count))
        return false;

    if (_pCtx != nullptr) {
        Global::DataType dt = (Global::DataType) _pCtx->getInt("dataType", Global::UNDEFINED);

        if ((dt == Global::MULTIMEDIA) || (dt == Global::UTF8))
            return false;

        if ((dt == Global::EXE) || (dt == Global::BIN))
            return false;
    }


    // TODO
    return true;
}


bool AliasCodec::inverse(SliceArray<byte>& input, SliceArray<byte>& output, int count)
{
    if (count == 0)
        return true;

    if (!SliceArray<byte>::isValid(input))
        throw invalid_argument("Alias codec: Invalid input block");

    if (!SliceArray<byte>::isValid(output))
        throw invalid_argument("Alias codec: Invalid output block");

    // TODO

    return true;
}
