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

#include "HuffmanCommon.hpp"

using namespace kanzi;

// Return the number of codes generated
// codes and symbols are updated
int HuffmanCommon::generateCanonicalCodes(const uint16 sizes[], uint codes[], uint symbols[], int count)
{
    if (count > 1) {
        byte buf[BUFFER_SIZE] = { byte(0) };

        for (int i = 0; i < count; i++) {
            const uint s = symbols[i];

            if ((s > 255) || (sizes[s] > MAX_SYMBOL_SIZE))
                return -1;

            buf[((sizes[s] - 1) << 8) | s] = byte(1);
        }

        for (int i = 0, n = 0; n < count; i++) {
            if (buf[i] == byte(0))
                continue;

            symbols[n++] = i & 0xFF;
        }
    }

    int curLen = sizes[symbols[0]];

    for (int i = 0, code = 0; i < count; i++) {
        const int s = symbols[i];

        if (sizes[s] > curLen) {
            code <<= (sizes[s] - curLen);
            curLen = sizes[s];
        }

        codes[s] = code;
        code++;
    }

    return count;
}
