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


#include "Global.hpp"
#include "IllegalArgumentException.hpp"

using namespace kanzi;


// array with 256 elements: int(Math.log2(x-1))
const int Global::LOG2[] = {
    0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8,
};

// array with 256 elements: 4096*Math.log2(x)
const int Global::LOG2_4096[] = {
         0,     0,  4096,  6492,  8192,  9511, 10588, 11499, 12288, 12984,
     13607, 14170, 14684, 15157, 15595, 16003, 16384, 16742, 17080, 17400,
     17703, 17991, 18266, 18529, 18780, 19021, 19253, 19476, 19691, 19898,
     20099, 20292, 20480, 20662, 20838, 21010, 21176, 21338, 21496, 21649,
     21799, 21945, 22087, 22226, 22362, 22495, 22625, 22752, 22876, 22998,
     23117, 23234, 23349, 23462, 23572, 23680, 23787, 23892, 23994, 24095,
     24195, 24292, 24388, 24483, 24576, 24668, 24758, 24847, 24934, 25021,
     25106, 25189, 25272, 25354, 25434, 25513, 25592, 25669, 25745, 25820,
     25895, 25968, 26041, 26112, 26183, 26253, 26322, 26390, 26458, 26525,
     26591, 26656, 26721, 26784, 26848, 26910, 26972, 27033, 27094, 27154,
     27213, 27272, 27330, 27388, 27445, 27502, 27558, 27613, 27668, 27722,
     27776, 27830, 27883, 27935, 27988, 28039, 28090, 28141, 28191, 28241,
     28291, 28340, 28388, 28437, 28484, 28532, 28579, 28626, 28672, 28718,
     28764, 28809, 28854, 28898, 28943, 28987, 29030, 29074, 29117, 29159,
     29202, 29244, 29285, 29327, 29368, 29409, 29450, 29490, 29530, 29570,
     29609, 29649, 29688, 29726, 29765, 29803, 29841, 29879, 29916, 29954,
     29991, 30027, 30064, 30100, 30137, 30172, 30208, 30244, 30279, 30314,
     30349, 30384, 30418, 30452, 30486, 30520, 30554, 30587, 30621, 30654,
     30687, 30719, 30752, 30784, 30817, 30849, 30880, 30912, 30944, 30975,
     31006, 31037, 31068, 31099, 31129, 31160, 31190, 31220, 31250, 31280,
     31309, 31339, 31368, 31397, 31426, 31455, 31484, 31513, 31541, 31569,
     31598, 31626, 31654, 31681, 31709, 31737, 31764, 31791, 31818, 31846,
     31872, 31899, 31926, 31952, 31979, 32005, 32031, 32058, 32084, 32109,
     32135, 32161, 32186, 32212, 32237, 32262, 32287, 32312, 32337, 32362,
     32387, 32411, 32436, 32460, 32484, 32508, 32533, 32557, 32580, 32604,
     32628, 32651, 32675, 32698, 32722, 32745, 32768
};


//  65536/(1 + exp(-alpha*x))
const int Global::INV_EXP[] = {
    // alpha = 0.55
    0, 17, 30, 51, 89, 154, 267, 461, 795, 1366,
    2331, 3938, 6537, 10558, 16367, 23977, 32768, 41559, 49169, 54978,
    58999, 61598, 63205, 64170, 64741, 65075, 65269, 65382, 65447, 65485,
    65506, 65519, 65526
};

const int* Global::SQUASH = Global::initSquash();

const int* Global::initSquash()
{
    int* res = new int[4096];

    for (int x = -2047; x <= 2047; x++) {
       int w = x & 127;
       int y = (x >> 7) + 16;
       res[x+2047] = (INV_EXP[y] * (128 - w) + INV_EXP[y + 1] * w) >> 11;
    }

    return res;
}

const int* Global::STRETCH = Global::initStretch();

const int* Global::initStretch()
{
    int* res = new int[4096];
    int n = 0;

    for (int x = -2047; (x <= 2047) && (n < 4096); x++) {
        const int sq = squash(x);

        while (n <= sq)
            res[n++] = x;
    }

    res[4095] = 2047;
    return res;
}


// Return 1024 * log2(x)
// Max error is around 0.1%
int Global::log2_1024(uint32 x) THROW
{
    if (x == 0)
        throw IllegalArgumentException("Cannot calculate log of a negative or null value");

    if (x < 256)
        return (Global::LOG2_4096[x]+2) >> 2;

    const int log = _log2(x);

    if ((x & (x - 1)) == 0)
        return log << 10;
    
    return ((log-7)*1024) + ((LOG2_4096[x>>(log-7)]+2) >> 2);
}


int Global::log2(uint32 x) THROW
{
    if (x == 0)
        throw IllegalArgumentException("Cannot calculate log of a negative or null value");

    return _log2(x);
}

// If withTotal is true, the last spot in each frequencies order 0 array is for the total
void Global::computeHistogram(byte block[], int end, uint freqs[], bool isOrder0, bool withTotal)
{
    const int32 dim = (isOrder0 == true) ? 1 : 256;
    const int mult = (withTotal == true) ? 257 : 256;
    memset(freqs, 0, dim * mult * sizeof(int));

    if (isOrder0 == true) {
        uint* f = &freqs[0];
        uint8* p = (uint8*)&block[0];
        const int end8 = end & -8;

        if (withTotal == true)
           f[256] = end;

        for (int i = 0; i < end8; i += 8) {
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
            f[*p]++; p++;
        }

        for (int i = end8; i < end; i++, p++)
            f[*p]++;
    }
    else { // Order 1
        uint prv = 0;
        uint8* p = (uint8*)&block[0];

        if (withTotal == true) {
           for (int i = 0; i < end; i++) {
               freqs[prv + uint(p[i])]++;
               freqs[prv + 256]++;
               prv = 257 * uint(p[i]);
           }
        } else {
           for (int i = 0; i < end; i++) {
               freqs[prv + uint(p[i])]++;
               prv = 256 * uint(p[i]);
           }
        }
    }

}

void Global::computeJobsPerTask(int jobsPerTask[], int jobs, int tasks)
{
	if ((jobs <= 0) || (tasks <= 0))
		return;

	int q = (jobs <= tasks) ? 1 : jobs / tasks;
	int r = (jobs <= tasks) ? 0 : jobs - q * tasks;

	for (int i = 0; i < tasks; i++)
		jobsPerTask[i] = q;

	int n = 0;

	while (r != 0) {
		jobsPerTask[n]++;
		r--;
		n++;

		if (n == tasks)
			n = 0;
	}
}