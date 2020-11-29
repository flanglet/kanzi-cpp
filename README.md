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

Use at your own risk. Always keep a backup of your files. The bitstream format is not yet finalized.

[![Build Status](https://travis-ci.org/flanglet/kanzi-cpp.svg?branch=master)](https://travis-ci.org/flanglet/kanzi-cpp)

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/flanglet/kanzi-cpp.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/flanglet/kanzi-cpp/context:cpp)

<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>


Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04.05

g++ version 8.4.0

Kanzi version 1.8 C++ implementation. Block size is 100 MB. 


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   211,938,580    |	
|Gzip 1.6	-4                  |        3.4      |       1.1       |    71,045,115    |        
|**Kanzi -l 1**               |  	   **1.8** 	  |     **0.9**     |  **69,840,720**  |
|**Kanzi -l 1 -j 6**          |  	   **0.7** 	  |     **0.3**     |  **69,840,720**  |
|Zstd 1.4.5 -2                |	       0.7      |       0.3       |    69,636,234    |
|Gzip 1.6	-5                  |        4.4      |       1.1       |    69,143,980    |        
|Brotli 1.0.7 -2              |        4.4      |       2.0       |    68,033,377    |
|Gzip 1.6	-9                  |       14.3      |       1.0       |    67,631,990    |        
|**Kanzi -l 2**               |	     **4.0**	  |     **2.0**     |  **60,147,109**  |
|**Kanzi -l 2 -j 6**          |	     **1.4**	  |     **0.6**     |  **60,147,109**  |
|Zstd 1.4.5 -13               |	      16.0      |       0.3       |    58,125,865    |
|Orz 1.5.0                    |	       7.6      |       2.0       |    57,564,831    |
|Brotli 1.0.7 -9              |       92.2      |       1.7       |    56,289,305    |
|Lzma 5.2.2 -3	              |       24.3	    |       2.4       |    55,743,540    |
|**Kanzi -l 3**               |	     **7.7**	  |     **4.7**     |  **54,996,910**  |
|**Kanzi -l 3 -j 6**          |	     **2.8**	  |     **1.6**     |  **54,996,910**  |
|Bzip2 1.0.6 -9	              |       14.9      |       5.2       |    54,506,769	   |
|Zstd 1.4.5 -19	              |       61.8      |       0.3       |    53,261,006    |
|Zstd 1.4.5 -19	-T6           |       53.4      |       0.3       |    53,261,006    |
|**Kanzi -l 4**               |	    **12.3**	  |     **7.3**     |  **51,739,977**  |
|**Kanzi -l 4 -j 6**          |      **4.4**    |     **2.4**     |  **51,739,977**  |
|Lzma 5.2.2 -9                |       65.0	    |       2.4       |    48,780,457    |
|**Kanzi -l 5**	              |     **14.8**    |     **9.2**     |  **48,067,650**  |
|**Kanzi -l 5 -j 6**          |      **5.3**    |     **2.9**     |  **48,067,650**  |
|**Kanzi -l 6**               |     **20.5**	  |    **17.2**     |  **46,541,768**  |
|**Kanzi -l 6 -j 6**          |      **6.0**	  |     **5.6**     |  **46,541,768**  |
|Tangelo 2.4	                |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1             |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12            |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 7**               |     **49.5**	  |    **51.4**     |  **41,804,239**  |
|**Kanzi -l 7 -j 6**          |     **17.6**	  |    **16.8**     |  **41,804,239**  |
|Tangelo 2.0	                |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 8**               |     **73.8**	  |    **76.0**     |  **40,423,483**  |
|**Kanzi -l 8 -j 6**          |     **28.3**	  |    **29.0**     |  **40,423,483**  |
|zpaq v7.14 m5 t1             |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12            |	     344.3	    |     350.4       |    39,112,924    |



enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04.05

g++ version 8.4.0

Kanzi version 1.8 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |	
|**Kanzi -l 1**               |  	  **1.24** 	  |    **0.65**     |  **32,654,135**  |
|**Kanzi -l 2**               |     **2.21**    |    **1.13**     |  **27,410,862**  |        
|**Kanzi -l 3**               |	    **3.88**    |    **2.42**     |  **25,670,935**  |
|**Kanzi -l 4**               |	    **5.46**	  |    **2.94**     |  **22,481,393**  |
|**Kanzi -l 5**               |	    **7.47**	  |    **4.10**     |  **21,232,214**  |
|**Kanzi -l 6**               |	    **9.46**	  |    **6.70**     |  **20,951,582**  |
|**Kanzi -l 7**               |	   **19.25**	  |   **19.54**     |  **19,515,358**  |
|**Kanzi -l 8**               |	   **27.63**	  |   **28.70**     |  **19,099,778**  |


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
Multithreading is supported.

### Linux
Go to the source directory and run 'make clean && make'. The Makefile contains all the necessary
targets. Build successfully on Ubuntu 18.04 with g++ 8.4.0 and g++ 9.3.0, clang++ 10.0.0
and icc 19.0.0.117. Multithreading is supported with g++ version 5.0.0 or newer.
Compiled successfully against C++03, C++11, C++14, C++17.
