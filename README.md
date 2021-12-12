kanzi
=====


Kanzi is a modern, modular, portable and efficient lossless data compressor implemented in C++.

* modern: state-of-the-art algorithms are implemented and multi-core CPUs can take advantage of the built-in multi-threading.
* modular: entropy codec and a combination of transforms can be provided at runtime to best match the kind of data to compress.
* portable: many OSes, compilers and C++ versions are supported (see below).
* expandable: clean design with heavy use of interfaces as contracts makes integrating and expanding the code easy. No dependencies.
* efficient: the code is optimized for efficiency (trade-off between compression ratio and speed).

Kanzi supports a wide range of compression ratios and can compress many files more than most common compressors (at the cost of decompression speed).
It is not compatible with standard compression formats.



For more details, check https://github.com/flanglet/kanzi-cpp/wiki.

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

Use at your own risk. Always keep a backup of your files.

![Build Status](https://github.com/flanglet/kanzi-cpp/actions/workflows/c-cpp.yml/badge.svg)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/flanglet/kanzi-cpp.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/flanglet/kanzi-cpp/context:cpp)

<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>


Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 20.04

clang++ 10.0.0, tcmalloc

Kanzi version 2.0 C++ implementation. Block size is 100 MB. 


|        Compressor               | Encoding (sec)  | Decoding (sec)  |    Size          |
|---------------------------------|-----------------|-----------------|------------------|
|Original     	                  |                 |                 |   211,938,580    |
|Zstd 1.5.0 -2 --long=30          |	       0.9      |       0.3       |    68,745,610    |
|Zstd 1.5.0 -2 -T6 --long=30      |	       0.4      |       0.3       |    68,745,610    |
|**Kanzi -l 1**                   |  	   **1.6**    |     **0.7**     |  **68,471,355**  |
|**Kanzi -l 1 -j 6**              |    	 **0.6**    |     **0.4**     |  **68,471,355**  |
|Pigz 1.6 -6 -p6                  |        1.4      |       1.4       |    68,237,849    |
|Gzip 1.6 -6                      |        6.1      |       1.1       |    68,227,965    |
|Brotli 1.0.9 -2 --large_window=30|        1.5      |       0.8       |    68,033,377    |
|Pigz 1.6 -9 -p6                  |        3.0      |       1.6       |    67,656,836    |
|Gzip 1.6 -9                      |       14.0      |       1.0       |    67,631,990    |
|**Kanzi -l 2**                   |	     **2.2**    |     **0.8**     |  **64,522,501**  |
|**Kanzi -l 2 -j 6**              |      **1.0**    |     **0.4**     |  **64,522,501**  |
|Brotli 1.0.9 -4 --large_window=30|        4.1      |       0.7       |    64,267,169    |
|Zstd 1.4.8 -9 --long=30          |        5.3      |       0.3       |    59,937,600    |
|Zstd 1.4.8 -9 -T6 --long=30      |	       2.8      |       0.3       |    59,937,600    |
|**Kanzi -l 3**                   |	     **3.4**    |     **1.3**     |  **59,652,799**  |
|**Kanzi -l 3 -j 6**              |	     **1.1**    |     **0.6**     |  **59,652,799**  |
|Zstd 1.4.8 -13 --long=30         |	      16.0      |       0.3       |    58,065,257    |
|Zstd 1.4.8 -13 -T6 --long=30     |	       9.2      |       0.3       |    58,065,257    |
|Orz 1.5.0                        |	       7.7      |       2.0       |    57,564,831    |
|Brotli 1.0.9 -9 --large_window=30|       36.7      |       0.7       |    56,232,817    |
|Lzma 5.2.2 -3	                  |       24.1	    |       2.6       |    55,743,540    |
|**Kanzi -l 4**                   |	     **6.2**    |     **4.0**     |  **54,998,230**  |
|**Kanzi -l 4 -j 6**              |	     **2.0**    |     **1.3**     |  **54,998,230**  |
|Bzip2 1.0.6 -9	                  |       14.9      |       5.2       |    54,506,769	   |
|Zstd 1.5.0 -19 --long=30	        |       59.7      |       0.4       |    52,773,547    |
|Zstd 1.5.0 -19	-T6 --long=30     |       59.7      |       0.4       |    52,773,547    |
|**Kanzi -l 5**                   |	    **11.4**    |     **4.5**     |  **51,760,234**  |
|**Kanzi -l 5 -j 6**              |      **4.1**    |     **1.7**     |  **51,760,234**  |
|Brotli 1.0.9 --large_window=30   |      356.2	    |       0.9       |    49,383,136    |
|Lzma 5.2.2 -9                    |       65.6	    |       2.5       |    48,780,457    |
|**Kanzi -l 6**	                  |     **13.8**    |     **6.2**     |  **48,067,980**  |
|**Kanzi -l 6 -j 6**              |      **4.9**    |     **2.1**     |  **48,067,980**  |
|BCM 1.65 -b100                   |       15.5      |      21.1       |    46,506,716    |
|**Kanzi -l 7**                   |     **17.0**    |    **11.2**     |  **46,446,999**  |
|**Kanzi -l 7 -j 6**              |      **5.3**    |     **4.6**     |  **46,446,999**  |
|Tangelo 2.4	                    |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1                 |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12                |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 8**                   |     **46.5**    |    **48.3**     |  **41,830,871**  |
|**Kanzi -l 8 -j 6**              |     **17.3**    |    **15.7**     |  **41,830,871**  |
|Tangelo 2.0	                    |      302.0      |     310.9       |    41,267,068    |
|**Kanzi -l 9**                   |     **67.4**    |    **70.4**     |  **40,369,883**  |
|**Kanzi -l 9 -j 6**              |     **27.9**    |    **29.3**     |  **40,369,883**  |
|zpaq v7.14 m5 t1                 |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12                |	     344.3	    |     350.4       |    39,112,924    |


enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 20.04

clang++ 10.0.0, tcmalloc

Kanzi version 2.0 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |
|**Kanzi -l 1**               |     **1.21**    |    **0.56**     |  **32,650,127**  |
|**Kanzi -l 2**               |     **1.54**    |    **0.56**     |  **31,018,886**  |
|**Kanzi -l 3**               |     **2.04**    |    **0.81**     |  **27,330,407**  |
|**Kanzi -l 4**               |	    **3.31**    |    **2.07**     |  **25,670,919**  |
|**Kanzi -l 5**               |	    **5.16**    |    **1.84**     |  **22,490,796**  |
|**Kanzi -l 6**               |	    **6.97**    |    **2.80**     |  **21,232,303**  |
|**Kanzi -l 7**               |	    **8.93**    |    **5.12**     |  **20,935,522**  |
|**Kanzi -l 8**               |	   **18.43**    |   **18.59**     |  **19,671,830**  |
|**Kanzi -l 9**               |	   **26.85**    |   **27.65**     |  **19,097,962**  |


Build Kanzi
-----------

The C++ code can be built on Windows with Visual Studio, Linux, macOS and Android with g++ and/or clang++.
Porting to other operating systems should be straightforward.

### Visual Studio 2008
Unzip the file "Kanzi_VS2008.zip" in place.
The project generates a Windows 32 binary. Multithreading is not supported with this version.

### Visual Studio 2017
Unzip the file "Kanzi_VS2017.zip" in place.
The project generates a Windows 64 binary. Multithreading is supported with this version.

### mingw-w64
Go to the source directory and run 'make clean && mingw32-make.exe'. The Makefile contains 
all the necessary targets. Tested successfully on Win64 with mingw-w64 g++ 8.1.0. 
Multithreading is supported. Compiled successfully with C++11, C++14, C++17.

### Linux
Go to the source directory and run 'make clean && make'. The Makefile contains all the necessary
targets. Build successfully on Ubuntu with g++ 8.4.0, g++ 9.3.0, g++ 10.3.0, clang++ 10.0.0
and icc 19.0.0.117. Multithreading is supported with g++ version 5.0.0 or newer.
Compiled successfully with C++11, C++14, C++17.

### BSD
The makefile uses the gnu-make syntax. First, make sure gmake is present (or install it: 'pkg_add gmake').
Go to the source directory and run 'gmake clean && gmake'. The Makefile contains all the necessary
targets.
