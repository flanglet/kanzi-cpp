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

#include <iostream>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "../function/SRT.hpp"
#include "../function/RLT.hpp"
#include "../function/ZRLT.hpp"
#include "../function/LZCodec.hpp"
#include "../function/ROLZCodec.hpp"

using namespace std;
using namespace kanzi;

static Function<byte>* getByteFunction(string name)
{
    if (name.compare("SRT") == 0)
        return new SRT();

    if (name.compare("RLT") == 0)
        return new RLT();

    if (name.compare("ZRLT") == 0)
        return new ZRLT();

    if (name.compare("LZ") == 0)
        return new LZCodec();

    if (name.compare("ROLZ") == 0)
        return new ROLZCodec();

    if (name.compare("ROLZX") == 0) {
        Context ctx;
        ctx.putString("transform", "ROLZX");
        return new ROLZCodec(ctx);
    }

    cout << "No such byte function: " << name << endl;
    return nullptr;
}

int testFunctionsCorrectness(const string& name)
{
    srand((uint)time(nullptr));

    cout << endl
         << "Correctness for " << name << endl;
    int mod = (name == "ZRLT") ? 5 : 256;
    int res = 0;

    for (int ii = 0; ii < 20; ii++) {
        cout << endl
             << "Test " << ii << endl;
        int size = 32;
        byte values[80000];

        if (ii == 0) {
            byte arr[] = {
                (byte)0, (byte)1, (byte)2, (byte)2, (byte)2, (byte)2, (byte)7, (byte)9,
                (byte)9, (byte)16, (byte)16, (byte)16, (byte)1, (byte)3, (byte)3, (byte)3,
                (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3,
                (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3, (byte)3
            };

            memcpy(values, &arr[0], size);
        }
        else if (ii == 1) {
            size = 80000;
            byte arr[80000];
            arr[0] = byte(1);

            for (int i = 1; i < 80000; i++)
                arr[i] = byte(8);

            memcpy(values, &arr[0], size);
        }
        else if (ii == 2) {
            size = 8;
            byte arr[] = { (byte)0, (byte)0, (byte)1, (byte)1, (byte)2, (byte)2, (byte)3, (byte)3 };
            memcpy(values, &arr[0], size);
        }
        else if (ii == 3) {
            // For RLT
            size = 512;
            byte arr[512];

            for (int i = 0; i < 256; i++) {
                arr[2 * i] = byte(i);
                arr[2 * i + 1] = byte(i);
            }

            arr[1] = byte(255); // force RLT escape to be first symbol
            memcpy(values, &arr[0], size);
        }
        else if (ii == 4) {
            // Lots of zeros
            size = 1024;
            byte arr[1024];

            for (int i = 0; i < size; i++) {
                int val = rand() % 100;

                if (val >= 33)
                    val = 0;

                arr[i] = byte(val);
            }

            memcpy(values, &arr[0], size);
        }
        else if (ii == 5) {
            // Lots of zeros
            size = 2048;
            byte arr[2048];

            for (int i = 0; i < size; i++) {
                int val = rand() % 100;

                if (val >= 33)
                    val = 0;

                arr[i] = byte(val);
            }

            memcpy(values, &arr[0], size);
        }
        else if (ii == 6) {
            // Totally random
            size = 512;
            byte arr[512];

            // Leave zeros at the beginning for ZRLT to succeed
            for (int j = 20; j < 512; j++)
                arr[j] = byte(rand() % mod);

            memcpy(values, &arr[0], size);
        }
        else {
            size = 1024;
            byte arr[1024];

            // Leave zeros at the beginning for ZRLT to succeed
            int idx = 20;

            while (idx < 1024) {
                int len = rand() % 40;

                if (len % 3 == 0)
                    len = 1;

                byte val = byte(rand() % mod);
                int end = (idx + len) < size ? idx + len : size;

                for (int j = idx; j < end; j++)
                    arr[j] = val;

                idx += len;
            }

            memcpy(values, &arr[0], size);
        }

        Function<byte>* f = getByteFunction(name);

        if (f == nullptr)
            return 1;

        byte* input = new byte[size];
        byte* output = new byte[f->getMaxEncodedLength(size)];
        byte* reverse = new byte[size];
        SliceArray<byte> iba1(input, size, 0);
        SliceArray<byte> iba2(output, f->getMaxEncodedLength(size), 0);
        SliceArray<byte> iba3(reverse, size, 0);
        memset(output, 0xAA, f->getMaxEncodedLength(size));
        memset(reverse, 0xAA, size);
        int count;

        for (int i = 0; i < size; i++)
            input[i] = values[i];

        cout << endl
             << "Original: " << endl;

        if (ii == 1) {
            cout << "1 8 (" << size << " times)";
        }
        else {
            for (int i = 0; i < size; i++)
                cout << (int(input[i]) & 0xFF) << " ";
        }

        if (f->forward(iba1, iba2, size) == false) {
            if ((iba1._index != size) || (iba2._index >= iba1._index)) {
                cout << endl
                     << "No compression (ratio > 1.0), skip reverse" << endl;
                continue;
            }

            cout << endl
                 << "Encoding error" << endl;
            res = 1;
            goto End;
        }

        if ((iba1._index != size) || (iba1._index < iba2._index)) {
            cout << endl
                 << "No compression (ratio > 1.0), skip reverse" << endl;
            continue;
        }

        delete f;
        cout << endl;
        cout << "Coded: " << endl;

        for (int i = 0; i < iba2._index; i++)
            cout << (int(output[i]) & 0xFF) << " ";

        cout << " (Compression ratio: " << (iba2._index * 100 / size) << "%)" << endl;
        f = getByteFunction(name);
        count = iba2._index;
        iba1._index = 0;
        iba2._index = 0;
        iba3._index = 0;

        if (f->inverse(iba2, iba3, count) == false) {
            cout << "Decoding error" << endl;
            res = 1;
            goto End;
        }

        cout << "Decoded: " << endl;

        if (ii == 1) {
            cout << "1 8 (" << size << " times)";
        }
        else {
            for (int i = 0; i < size; i++)
                cout << (int(reverse[i]) & 0xFF) << " ";
        }

        cout << endl;

        for (int i = 0; i < size; i++) {
            if (input[i] != reverse[i]) {
                cout << "Different (index " << i << ": ";
                cout << (int(input[i]) & 0xFF) << " - " << (int(reverse[i]) & 0xFF);
                cout << ")" << endl;
                res = 1;
                goto End;
            }
        }

        cout << endl
             << "Identical" << endl
             << endl;

    End:
        delete f;
        delete[] input;
        delete[] output;
        delete[] reverse;
    }

    return res;
}

int testFunctionsSpeed(const string& name)
{
    // Test speed
    srand((uint)time(nullptr));
    int iter = (name.rfind("ROLZ", 0) == 0) ? 2000 : ((name == "SRT") ? 4000 : 50000);
    int size = 30000;
    int res = 0;

    cout << endl
         << endl
         << "Speed test for " << name << endl;
    cout << "Iterations: " << iter << endl;
    cout << endl;
    byte input[50000];
    byte output[50000];
    byte reverse[50000];
    Function<byte>* f = getByteFunction(name);

    if (f == nullptr)
        return 1;

    SliceArray<byte> iba1(input, size, 0);
    SliceArray<byte> iba2(output, f->getMaxEncodedLength(size), 0);
    SliceArray<byte> iba3(reverse, size, 0);
    int mod = (name == "ZRLT") ? 5 : 256;
    delete f;

    for (int jj = 0; jj < 3; jj++) {
        // Generate random data with runs
        // Leave zeros at the beginning for ZRLT to succeed
        int n = iter / 20;

        while (n < size) {
            byte val = byte(rand() % mod);
            input[n++] = val;
            int run = rand() % 256;
            run -= 220;

            while ((--run > 0) && (n < size))
                input[n++] = val;
        }

        clock_t before, after;
        double delta1 = 0;
        double delta2 = 0;

        for (int ii = 0; ii < iter; ii++) {
            Function<byte>* f = getByteFunction(name);
            iba1._index = 0;
            iba2._index = 0;
            before = clock();

            if (f->forward(iba1, iba2, size) == false) {
                if ((iba1._index != size) || (iba2._index >= iba1._index)) {
                   cout << endl
                        << "No compression (ratio > 1.0), skip reverse" << endl;
                   continue;
                }

                cout << "Encoding error" << endl;
                delete f;
                continue;
            }

            after = clock();
            delta1 += (after - before);
            delete f;
        }

        for (int ii = 0; ii < iter; ii++) {
            Function<byte>* f = getByteFunction(name);
            int count = iba2._index;
            iba3._index = 0;
            iba2._index = 0;
            before = clock();

            if (f->inverse(iba2, iba3, count) == false) {
                cout << "Decoding error" << endl;
                delete f;
                return 1;
            }

            after = clock();
            delta2 += (after - before);
            delete f;
        }

        int idx = -1;

        // Sanity check
        for (int i = 0; i < iba1._index; i++) {
            if (iba1._array[i] != iba3._array[i]) {
                idx = i;
                break;
            }
        }

        if (idx >= 0) {
            cout << "Failure at index " << idx << " (" << (int)iba1._array[idx];
            cout << "<->" << (int)iba3._array[idx] << ")" << endl;
            res = 1;
        }

        double prod = (double)iter * (double)size;
        double b2MB = (double)1 / (double)(1024 * 1024);
        double d1_sec = (double)delta1 / CLOCKS_PER_SEC;
        double d2_sec = (double)delta2 / CLOCKS_PER_SEC;
        cout << name << " encoding [ms]: " << (int)(d1_sec * 1000) << endl;
        cout << "Throughput [MB/s]: " << (int)(prod * b2MB / d1_sec) << endl;
        cout << name << " decoding [ms]: " << (int)(d2_sec * 1000) << endl;
        cout << "Throughput [MB/s]: " << (int)(prod * b2MB / d2_sec) << endl;
    }

    return res;
}

#ifdef __GNUG__
int main(int argc, const char* argv[])
#else
int TestFunctions_main(int argc, const char* argv[])
#endif
{
    string str;

    if (argc == 1) {
        str = "-TYPE=ALL";
    }
    else {
        str = argv[1];
    }

    transform(str.begin(), str.end(), str.begin(), ::toupper);
    int res = 0;

    if (str.compare(0, 6, "-TYPE=") == 0) {
        str = str.substr(6);

        if (str.compare("ALL") == 0) {
            cout << endl
                 << endl
                 << "TestLZ" << endl;
            res |= testFunctionsCorrectness("LZ");
            res |= testFunctionsSpeed("LZ");
            cout << endl
                 << endl
                 << "TestROLZ" << endl;
            res |= testFunctionsCorrectness("ROLZ");
            res |= testFunctionsSpeed("ROLZ");
            cout << endl
                 << endl
                 << "TestSRT" << endl;
            res |= testFunctionsCorrectness("SRT");
            res |= testFunctionsSpeed("SRT");
            cout << endl
                 << endl
                 << "TestRLT" << endl;
            res |= testFunctionsCorrectness("RLT");
            res |= testFunctionsSpeed("RLT");
            cout << endl
                 << endl
                 << "TestZRLT" << endl;
            res |= testFunctionsCorrectness("ZRLT");
            res |= testFunctionsSpeed("ZRLT");
        }
        else {
            cout << "Test" << str << endl;
            res |= testFunctionsCorrectness(str);
            res |= testFunctionsSpeed(str);
        }
    }

    return res;
}
