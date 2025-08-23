# Kanzi

Kanzi is a modern, modular, portable, and efficient lossless data compressor written in C++.

* Modern: Kanzi implements state-of-the-art compression algorithms and is built to fully utilize multi-core CPUs via built-in multi-threading.
* Modular: Entropy codecs and data transforms can be selected and combined at runtime to best suit the specific data being compressed.
* Portable: Supports a wide range of operating systems, compilers, and C++ standards (details below).
* Expandable: A clean, interface-driven design—with no external dependencies—makes Kanzi easy to integrate, extend, and customize.
* Efficient: Carefully optimized to balance compression ratio and speed for practical, high-performance usage.

Unlike most mainstream lossless compressors, Kanzi is not limited to a single compression paradigm. By combining multiple algorithms and techniques, it supports a broader range of compression ratios and adapts better to diverse data types.

Most traditional compressors underutilize modern hardware by running single-threaded—even on machines with many cores. Kanzi, in contrast, is concurrent by design, compressing multiple blocks in parallel across threads for significant performance gains. However, it is not compatible with standard compression formats.

It’s important to note that Kanzi is a data compressor, not an archiver. It includes optional checksums for verifying data integrity, but does not provide features like cross-file deduplication or data recovery mechanisms. That said, it produces a seekable bitstream—meaning one or more consecutive blocks can be decompressed independently, without needing to process the entire stream.

For more details, see [Wiki](https://github.com/flanglet/kanzi-cpp/wiki), [Q&A](https://github.com/flanglet/kanzi-cpp/wiki/q&a) and [DeepWiki](https://deepwiki.com/flanglet/kanzi-cpp/1-overview)

See how to reuse the C and C++ APIs: [here](https://github.com/flanglet/kanzi-cpp/wiki/Using-and-extending-the-code)

There is a Java implementation available here: https://github.com/flanglet/kanzi

There is a Go implementation available here: https://github.com/flanglet/kanzi-go

![Build Status](https://github.com/flanglet/kanzi-cpp/actions/workflows/c-cpp.yml/badge.svg)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/flanglet/kanzi-cpp)


## Why Kanzi

There are already many excellent, open-source lossless data compressors available.

If gzip is beginning to show its age, modern alternatives like **zstd** and **brotli** offer compelling replacements. Both are open-source, standardized, and used daily by millions. **Zstd** is especially notable for its exceptional speed and is often the best choice in general-purpose compression.

However, there are scenarios where **Kanzi** may offer superior performance:

While gzip, LZMA, brotli, and zstd are all based on LZ (Lempel-Ziv) compression, they are inherently limited in the compression ratios they can achieve. **Kanzi** goes further by incorporating **BWT (Burrows-Wheeler Transform)** and **CM (Context Modeling)**, which can outperform traditional LZ-based methods in certain cases.

LZ-based compressors are ideal for software distribution, where data is compressed once and decompressed many times, thanks to their fast decompression speeds—though they tend to be slower when compressing at higher ratios. But in other scenarios—such as real-time data generation, one-off data transfers, or backups—**compression speed becomes critical**. Here, Kanzi can shine.

**Kanzi** also features a suite of built-in, customizable data transforms tailored for specific data types (e.g., multimedia, UTF, text, DNA, etc.), which can be selectively applied during compression for better efficiency.

Furthermore, Kanzi is designed to **leverage modern multi-core CPUs** to boost performance.

Finally, **extensibility** is a key strength: implementing new transforms or entropy codecs—whether for experimentation or to improve performance on niche data types—is straightforward and developer-friendly.



## Benchmarks

Test machine:

Apple M3 24 GB Sonoma 14.6.1

Kanzi version 2.4.0 C++ implementation

On this machine, Kanzi uses 4 threads (half of CPUs by default).

bzip3 runs with 4threads. 

zstd and lz4 use 4 threads for compression and 1 for decompression, other compressors are single threaded.

The default block size at level 9 is 32MB, severely limiting the number of threads
in use, especially with enwik8, but all tests are performed with default values.


### silesia.tar

Download at http://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip

|        Compressor               |  Encoding (ms)  |  Decoding (ms)  |      Size        |
|---------------------------------|-----------------|-----------------|------------------|
|Original                         |                 |                 |   211,957,760    |
|**kanzi -l 1**                   |     **438**     |      **252**    |    80,245,856    |
|lz4 1.1.10 -T4 -4                |       527       |        121      |    79,919,901    |
|zstd 1.5.8 -T4 -2                |       147       |        150      |    69,410,383    |
|**kanzi -l 2**                   |     **326**     |      **270**    |    68,860,099    |
|brotli 1.1.0 -2                  |       907       |        402      |    68,039,159    |
|Apple gzip 430.140.2 -9          |     10406       |        273      |    67,648,481    |
|**kanzi -l 3**                   |     **584**     |      **344**    |    64,266,936    |
|zstd 1.5.8 -T4 -5                |       300       |        154      |    62,851,716    |
|**kanzi -l 4**                   |     **786**     |      **463**    |    61,131,554    |
|zstd 1.5.8 -T4 -9                |       752       |        137      |    59,190,090    |
|brotli 1.1.0 -6                  |      3596       |        340      |    58,557,128    |
|zstd 1.5.8 -T4 -13               |      4537       |        138      |    57,814,719    |
|brotli 1.1.0 -9                  |     19809       |        329      |    56,414,012    |
|bzip2 1.0.8 -9                   |      9673       |       3140      |    54,602,583    |
|**kanzi -l 5**                   |    **2067**     |     **1238**    |    54,025,588    |
|zstd 1.5.8 -T4 -19               |     20482       |        151      |    52,858,610    |
|**kanzi -l 6**                   |    **3003**     |     **2309**    |    49,521,392    |
|xz 5.8.1 -9                      |     48516       |       1594      |    48,774,000    |
|bsc 3.3.11 -T4                   |      1926       |       1650      |    47,963,044    |
|**kanzi -l 7**                   |    **3726**     |     **3298**    |    47,312,772    |
|bzip3 1.5.1.r3-g428f422 -j 4     |      8559       |       3948      |    47,256,794    |
|**kanzi -l 8**                   |   **14335**     |    **15637**    |    43,260,254    |
|**kanzi -l 9**                   |   **19727**     |    **21335**    |    41,858,030    |

### enwik8

Download at https://mattmahoney.net/dc/enwik8.zip

Apple M3 24 GB Sonoma 14.6.1

|   Compressor    | Encoding (ms)  | Decoding (ms)  |    Size      |
|-----------------|----------------|----------------|--------------|
|Original         |                |                |  100,000,000 |
|kanzi -l 1       |       271      |        135     |   43,644,013 |
|kanzi -l 2       |       196      |        142     |   37,570,404 |
|kanzi -l 3       |       350      |        200     |   32,466,232 |
|kanzi -l 4       |       372      |        249     |   29,536,517 |
|kanzi -l 5       |       720      |        478     |   26,523,940 |
|kanzi -l 6       |      1053      |        807     |   24,076,765 |
|kanzi -l 7       |      1704      |       1416     |   22,817,360 |
|kanzi -l 8       |      6544      |       6988     |   21,181,992 |
|kanzi -l 9       |      8194      |       9090     |   20,035,144 |



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
The makefile uses the gnu-make syntax. First, make sure gmake is present (or install it: 'pkg install gmake').
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

