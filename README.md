# Kanzi


Kanzi is a modern, modular, portable and efficient lossless data compressor implemented in C++.

* modern: state-of-the-art algorithms are implemented and multi-core CPUs can take advantage of the built-in multi-threading.
* modular: entropy codec and a combination of transforms can be provided at runtime to best match the kind of data to compress.
* portable: many OSes, compilers and C++ versions are supported (see below).
* expandable: clean design with heavy use of interfaces as contracts makes integrating and expanding the code easy. No dependencies.
* efficient: the code is optimized for efficiency (trade-off between compression ratio and speed).

Unlike the most common lossless data compressors, Kanzi uses a variety of different compression algorithms and supports a wider range of compression ratios as a result. Most usual compressors do not take advantage of the many cores and threads available on modern CPUs (what a waste!). Kanzi is multithreaded by design and uses several threads by default to compress blocks concurrently. It is not compatible with standard compression formats. Kanzi is a lossless data compressor, not an archiver. It uses checksums (optional but recommended) to validate data integrity but does not have a mechanism for data recovery. It also lacks data deduplication across files.

For more details, check https://github.com/flanglet/kanzi/wiki.

See how to reuse the C and C++ APIs here: https://github.com/flanglet/kanzi-cpp/wiki/Using-and-extending-the-code

There is a Java implementation available here: https://github.com/flanglet/kanzi

There is Go implementation available here: https://github.com/flanglet/kanzi-go

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

![Build Status](https://github.com/flanglet/kanzi-cpp/actions/workflows/c-cpp.yml/badge.svg)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=flanglet_kanzi-cpp&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=flanglet_kanzi-cpp)
<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>

## Why Kanzi

There are many excellent, open-source lossless data compressors available already.

If gzip is starting to show its age, zstd and brotli are open-source, standardized and used
daily by millions of people. Zstd is incredibly fast and probably the best choice in many cases.
There are a few scenarios where Kanzi could be a better choice:

- gzip, lzma, brotli, zstd are all LZ based. It means that they can reach certain compression
ratios only. Kanzi also makes use of BWT and CM which can compress beyond what LZ can do.

- These LZ based compressors are well suited for software distribution (one compression / many decompressions)
due to their fast decompression (but low compression speed at high compression ratios). 
There are other scenarios where compression speed is critical: when data is generated before being compressed and consumed
(one compression / one decompression) or during backups (many compressions / one decompression).

- Kanzi has built-in customized data transforms (multimedia, utf, text, dna, ...) that can be chosen and combined 
at compression time to better compress specific kinds of data.

- Kanzi can take advantage of the multiple cores of a modern CPU to improve performance

- It is easy to implement a new transform or entropy codec to either test an idea or improve
compression ratio on specific kinds of data.


## Benchmarks

Test machine:

AWS c5a8xlarge: AMD EPYC 7R32 (32 vCPUs), 64 GB RAM

clang++ 14.0.0-1ubuntu1.1

Ubuntu 22.04.3 LTS

Kanzi version 2.2 C++ implementation.

On this machine, Kanzi can use up to 16 threads depending on compression level
(the default block size at level 9 is 32MB, severly limiting the number of threads
in use, especially with enwik8, but all tests are performed with default values).
bzip3 uses 16 threads. zstd can use 2 for compression, other compressors
are single threaded.


### silesia.tar

|        Compressor               | Encoding (sec)  | Decoding (sec)  |    Size          |
|---------------------------------|-----------------|-----------------|------------------|
|Original     	                  |                 |                 |   211,957,760    |
|**Kanzi -l 1**                   |   	**0.284**   |    **0.185**    |  **80,284,705**  |
|Zstd 1.5.5 -2                    |	      0.761     |      0.286      |    69,590,245    |
|**Kanzi -l 2**                   |   	**0.310**   |    **0.215**    |  **68,231,498**  |
|Brotli 1.1.0 -2                  |       1.749     |      2.459      |    68,044,145    |
|Gzip 1.10 -9                     |      20.15      |      1.316      |    67,652,229    |
|**Kanzi -l 3**                   |   	**0.501**   |    **0.261**    |  **64,916,444**  |
|Zstd 1.5.5 -5                    |	      2.003     |      0.324      |    63,103,408    |
|**Kanzi -l 4**                   |   	**0.668**   |    **0.353**    |  **60,770,201**  |
|Zstd 1.5.5 -9                    |	      4.166     |      0.282      |    59,444,065    |
|Brotli 1.1.0 -6                  |      14.53      |      4.263      |    58,552,177    |
|Zstd 1.5.5 -15                   |	     19.15      |      0.276      |    58,061,115    |
|Brotli 1.1.0 -9                  |      70.07      |      7.149      |    56,408,353    |
|Bzip2 1.0.8 -9	                  |      16.94      |      6.734      |    54,572,500    |
|**Kanzi -l 5**                   |   	**1.775**   |    **0.920**    |  **54,051,139**  |
|Zstd 1.5.5 -19                   |	     92.82      |      0.302      |    52,989,654    |
|**Kanzi -l 6**                   |   	**2.614**   |    **1.289**    |  **49,517,823**  |
|Lzma 5.2.5 -9                    |      92.6       |      3.075      |    48,744,632    |
|**Kanzi -l 7**                   |   	**2.723**   |    **2.175**    |  **47,308,484**  |
|bzip3 1.3.2.r4-gb2d61e8 -j 16    |       2.682     |      3.221      |    47,237,088    |
|**Kanzi -l 8**                   |   	**7.487**   |    **7.875**    |  **43,247,248**  |
|**Kanzi -l 9**                   |    **19.29**    |   **19.43**     |  **41,807,179**  |
|zpaq 7.15 -m5 -t16               |     213.8       |    213.8        |    40,050,429    |


### enwik8

|      Compressor        | Encoding (sec)   | Decoding (sec)   |    Size          |
|------------------------|------------------|------------------|------------------|
|Original                |                  |                  |   100,000,000    |
|**Kanzi -l 1**          |     **0.189**    |    **0.107**     |  **43,747,730**  |
|**Kanzi -l 2**          |     **0.192**    |    **0.120**     |  **37,745,093**  |
|**Kanzi -l 3**          |     **0.321**    |    **0.146**     |  **33,839,184**  |
|**Kanzi -l 4**          |	   **0.349**    |    **0.198**     |  **29,598,635**  |
|**Kanzi -l 5**          |	   **0.570**    |    **0.342**     |  **26,527,955**  |
|**Kanzi -l 6**          |	   **0.710**    |    **0.554**     |  **24,076,669**  |
|**Kanzi -l 7**          |     **1.789**    |    **1.619**     |  **22,817,376**  |
|**Kanzi -l 8**          |	   **4.639**    |    **4.748**     |  **21,181,978**  |
|**Kanzi -l 9**          |	  **12.71**     |   **13.28**      |  **20,035,133**  |


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
targets. Build successfully on Ubuntu with many versions of clang++.
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


