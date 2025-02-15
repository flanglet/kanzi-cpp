# Kanzi


Kanzi is a modern, modular, portable and efficient lossless data compressor implemented in C++.

* modern: state-of-the-art algorithms are implemented and multi-core CPUs can take advantage of the built-in multi-threading.
* modular: entropy codec and a combination of transforms can be provided at runtime to best match the kind of data to compress.
* portable: many OSes, compilers and C++ versions are supported (see below).
* expandable: clean design with heavy use of interfaces as contracts makes integrating and expanding the code easy. No dependencies.
* efficient: the code is optimized for efficiency (trade-off between compression ratio and speed).

Unlike the most common lossless data compressors, Kanzi uses a variety of different compression algorithms and supports a wider range of compression ratios as a result. Most usual compressors do not take advantage of the many cores and threads available on modern CPUs (what a waste!). Kanzi is concurrent by design and uses threads to compress several blocks in parallel. It is not compatible with standard compression formats. 

Kanzi is a lossless data compressor, not an archiver. It uses checksums (optional but recommended) to validate data integrity but does not have a mechanism for data recovery. It also lacks data deduplication across files. However, Kanzi generates a bitstream that is seekable (one or several consecutive blocks can be decompressed without the need for the whole bitstream to be decompressed).

For more details, see [Wiki](https://github.com/flanglet/kanzi/wiki) and [Q&A](https://github.com/flanglet/kanzi/wiki/q&a)

See how to reuse the C and C++ APIs: [here](https://github.com/flanglet/kanzi-cpp/wiki/Using-and-extending-the-code)

There is a Java implementation available here: https://github.com/flanglet/kanzi

There is Go implementation available here: https://github.com/flanglet/kanzi-go

![Build Status](https://github.com/flanglet/kanzi-cpp/actions/workflows/c-cpp.yml/badge.svg)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Why Kanzi

There are many excellent, open-source lossless data compressors available already.

If gzip is starting to show its age, zstd and brotli are open-source, standardized and used
daily by millions of people. Zstd is incredibly fast and probably the best choice in many cases.
There are a few scenarios where Kanzi can be a better choice:

- gzip, lzma, brotli, zstd are all LZ based. It means that they can reach certain compression
ratios only. Kanzi also makes use of BWT and CM which can compress beyond what LZ can do.

- These LZ based compressors are well suited for software distribution (one compression / many decompressions)
due to their fast decompression (but low compression speed at high compression ratios). 
There are other scenarios where compression speed is critical: when data is generated before being compressed and consumed
(one compression / one decompression) or during backups (many compressions / one decompression).

- Kanzi has built-in customized data transforms (multimedia, utf, text, dna, ...) that can be chosen and combined 
at compression time to better compress specific kinds of data.

- Kanzi can take advantage of the multiple cores of a modern CPU to improve performance

- Implementing a new transform or entropy codec (to either test an idea or improve compression ratio on specific kinds of data) is simple.


## Benchmarks

Test machine:

AWS c5a8xlarge: AMD EPYC 7R32 (32 vCPUs), 64 GB RAM

Ubuntu clang++ version 15.0.7 + tcmalloc

Ubuntu 24.04 LTS

Kanzi version 2.3.0 C++ implementation

On this machine, Kanzi uses up to 16 threads (half of CPUs by default).

bzip3 and zpaq use 16 threads. 
zstd uses 16 threads for compression and 1 for decompression, other compressors are single threaded.

The default block size at level 9 is 32MB, severely limiting the number of threads
in use, especially with enwik8, but all tests are performed with default values.


### silesia.tar

Download at http://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip

|        Compressor               | Encoding (sec)  | Decoding (sec)  |    Size          |
|---------------------------------|-----------------|-----------------|------------------|
|Original     	                  |                 |                 |   211,957,760    |
|**Kanzi -l 1**                   |   	**0.263**   |    **0.231**    |  **80,277,212**  |
|Lz4 1.9.5 -4                     |       0.321     |      0.330      |    79,912,419    |
|Zstd 1.5.6 -2 -T16               |	      0.151     |      0.271      |    69,556,157    |
|**Kanzi -l 2**                   |   	**0.267**   |    **0.253**    |  **68,195,845**  |
|Brotli 1.1.0 -2                  |       1.749     |      0.761      |    68,041,629    |
|Gzip 1.12 -9                     |      20.09      |      1.403      |    67,652,449    |
|**Kanzi -l 3**                   |   	**0.446**   |    **0.287**    |  **65,613,695**  |
|Zstd 1.5.6 -5 -T16               |	      0.356     |      0.289      |    63,131,656    |
|**Kanzi -l 4**                   |   	**0.543**   |    **0.373**    |  **61,249,959**  |
|Zstd 1.5.5 -9 -T16               |	      0.690     |      0.278      |    59,429,335    |
|Brotli 1.1.0 -6                  |       8.388     |      0.677      |    58,571,909    |
|Zstd 1.5.6 -13 -T16              |	      3.244     |      0.272      |    58,041,112    |
|Brotli 1.1.0 -9                  |      70.07      |      0.761      |    56,376,419    |
|Bzip2 1.0.8 -9	                  |      16.94      |      6.734      |    54,572,500    |
|**Kanzi -l 5**                   |   	**1.627**   |    **0.883**    |  **54,039,773**  |
|Zstd 1.5.6 -19 -T16              |	     20.87      |      0.303      |    52,889,925    |
|**Kanzi -l 6**                   |   	**2.312**   |    **1.227**    |  **49,567,817**  |
|Lzma 5.4.5 -9                    |      95.97      |      3.172      |    48,745,354    |
|**Kanzi -l 7**                   |   	**2.686**   |    **2.553**    |  **47,520,629**  |
|bzip3 1.3.2.r4-gb2d61e8 -j 16    |       2.682     |      3.221      |    47,237,088    |
|**Kanzi -l 8**                   |   	**7.260**   |    **8.021**    |  **43,167,429**  |
|**Kanzi -l 9**                   |    **18.99**    |   **21.07**     |  **41,497,835**  |
|zpaq 7.15 -m5 -t16               |     213.8       |    213.8        |    40,050,429    |


### enwik8

Download at https://mattmahoney.net/dc/enwik8.zip

Tested on Ubuntu 22.04.4 LTS, i7-7700K CPU @ 4.20GHz, 32 GB RAM, clang-15, 4 threads (default)

|   Compressor    | Encoding (ms)  | Decoding (ms)  |  Size        |
|-----------------|----------------|----------------|--------------|
|Original         |                |                |  100,000,000 |
|Kanzi -l 1       |       251      |         87     |   43,746,017 |
|Kanzi -l 2       |       268      |        114     |   37,816,913 |
|Kanzi -l 3       |       512      |        175     |   33,865,383 |
|Kanzi -l 4       |       546      |        249     |   29,597,577 |
|Kanzi -l 5       |      1030      |        500     |   26,528,023 |
|Kanzi -l 6       |      1537      |        799     |   24,076,674 |
|Kanzi -l 7       |      2695      |       2045     |   22,817,373 |
|Kanzi -l 8       |      7217      |       7314     |   21,181,983 |
|Kanzi -l 9       |     11336      |      11574     |   20,035,138 |



### Round-trip scores for LZ

Below is a table showing silesia.tar compressed using different LZ compressors (no entropy) in single-threaded mode.

The efficiency score is computed as such: score(lambda) = compTime + 2 x decompTime + 10^-lambda x compSize

A lower score is better. Best scores are in bold.

Tested on Ubuntu 22.04.4 LTS, i7-7700K CPU @ 4.20GHz, 32 GB RAM, clang-15

|      Compressor          | Encoding (sec) | Decoding (sec)  |    Size          |  Score(5)  |  Score(6)  |  Score(7)  |
|--------------------------|----------------|-----------------|------------------|------------|------------|------------|
|FastLZ -2	               |     1.85	      |       0.84	    |     101114153	   |  1014.66	  |   104.63	 |   13.63    |
|Lizard 1.1.0 -11	         |     0.76	      |      	0.24	    |      93967850	   | 	 940.91   |	  95.20	   |   10.63    |
|Lz4 1.9.5 -2	-T1          |     0.81	      |      	0.21	    |    	 89208908	   |   893.32   |   90.44    |	 10.15    |
|Lzturbo 1.2 -11 -p0       |	   1.09	      |      	0.34	    |      88657053	   |	 888.35   |   90.43	   |	 10.64    |
|lzav	(1)                  |     0.52	      |       0.19	    |      88221200	   |	 883.12   |   89.13	   |	  9.73    |
|s2 -cpu 1	               |     0.81	      |      	0.40	    |	     86646819	   |	 868.08   |   88.25	   |	 10.27    |
|LZ4x 1.60 -2	             |     1.13	      |      	0.22	    |	     87883674	   |	 880.40   |   89.44	   |	 10.35    |
|lzav	(2)                  |     0.67	      |       TBD 	    |      86505609	   |	          |       	   |	          |
|Lizard 1.1.0 -12	         |     1.48	      |      	0.23	    |      86340434	   |	 865.35   |   88.29	   |	 10.58    |
|LZ4x 1.60 -3	             |     1.36	      |      	0.24	    |	     85483806	   |	 856.67   |   87.32	   |	 10.38    |
|Kanzi 2.3 -t lz -j 1	 (1) |     0.83	      |      	0.24	    |      83355862	   |	 834.87   |   84.67	   | ***9.65*** |
|Lzturbo 1.2 -12 -p0	     |     2.40	      |      	0.22	    |      83179291	   |	 834.63   |   86.02	   |	 11.16    |
|Kanzi 2.3 -t lz -j 1	 (2) |     0.99	      |      	0.35	    |      82652955	   |	 828.22   |   84.34	   |    9.96    |
|Kanzi 2.3 -t lzx -j 1 (1) |	   1.09	      |      	0.22	    |      81485228	   |   816.39   |   83.02    |	  9.68    |
|Lz4 1.9.5 -3	-T1          |     2.33	      |      	0.21	    |	     81441623	   |   817.17   |   84.19    |   10.90    |
|Kanzi 2.3 -t lzx -j 1 (2) |     1.52	      |      	0.35	    |	     79014650	   |***792.37***|***81.23*** |   10.12    |

References:

[FastLZ](https://github.com/ariya/FastLZ)
[Lizard](https://github.com/inikep/lizard)
[LZ4](https://github.com/lz4/lz4)
[S2](https://github.com/klauspost/compress)
[LZAV](https://github.com/avaneev/lzav)
[LZ4x](https://github.com/tomsim/lz4x)
[LZTurbo](https://sites.google.com/site/powturbo)

lz4@97291fc50

kanzi@af12d07f2

lzav@10f7e2ac

(1) processing 4MB blocks

(2) processing whole file at once


### More benchmarks

[Comprehensive lzbench benchmarks](https://github.com/flanglet/kanzi-cpp/wiki/Performance)

[More round trip scores](https://github.com/flanglet/kanzi-cpp/wiki/Round%E2%80%90trips-scores)


## Build Kanzi

The C++ code can be built on Windows with Visual Studio, Linux, macOS and Android with g++ and/or clang++.
There are no dependencies. Porting to other operating systems should be straightforward.

### Visual Studio 2008
Unzip the file "Kanzi_VS2008.zip" in place.
The solution generates a Windows 32 binary. Multithreading is not supported with this version.

### Visual Studio 2022
Unzip the file "Kanzi_VS2022.zip" in place.
The solution generates a Windows 64 binary and library. Multithreading is supported with this version.

### mingw-w64
Go to the source directory and run 'make clean && mingw32-make.exe kanzi'. The Makefile contains 
all the necessary targets. Tested successfully on Win64 with mingw-w64 g++ 8.1.0. 
Multithreading is supportedwith g++ version 5.0.0 or newer.
Builds successfully with C++11, C++14, C++17.

### Linux
Go to the source directory and run 'make clean && make kanzi'. The Makefile contains all the necessary
targets. Build successfully on Ubuntu with many versions of g++ and clang++.
Multithreading is supported with g++ version 5.0.0 or newer.
Builds successfully with C++98, C++11, C++14, C++17, C++20.

### MacOS
Go to the source directory and run 'make clean && make kanzi'. The Makefile contains all the necessary
targets. Build successfully on MacOs with several versions of clang++.
Multithreading is supported.

### BSD
The makefile uses the gnu-make syntax. First, make sure gmake is present (or install it: 'pkg_add gmake').
Go to the source directory and run 'gmake clean && gmake kanzi'. The Makefile contains all the necessary
targets. Multithreading is supported.

### Makefile targets
```
clean:     removes objects, libraries and binaries
kanzi:     builds the kanzi executable
lib:       builds static and dynamic libraries
test:      builds test binaries
all:       kanzi + lib + test
install:   installs libraries, headers and executable
uninstall: removes installed libraries, headers and executable
```

For those who prefer cmake, run the following commands:
```
mkdir build
cd build
cmake ..
make
```

Credits

Matt Mahoney,
Yann Collet,
Jan Ondrus,
Yuta Mori,
Ilya Muravyov,
Neal Burns,
Fabian Giesen,
Jarek Duda, 
Ilya Grebnov

Disclaimer

Use at your own risk. Always keep a copy of your original files.

