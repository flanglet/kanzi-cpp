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

[![Build Status](https://travis-ci.org/flanglet/kanzi-cpp.svg?branch=master)](https://travis-ci.org/flanglet/kanzi-cpp)

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/flanglet/kanzi-cpp.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/flanglet/kanzi-cpp/context:cpp)

<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>


Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 20.04

g++ version 9.3.0

Kanzi version 1.9 C++ implementation. Block size is 100 MB. 


|        Compressor               | Encoding (sec)  | Decoding (sec)  |    Size          |
|---------------------------------|-----------------|-----------------|------------------|
|Original     	                  |                 |                 |   211,938,580    |	
|Zstd 1.4.8 -2                    |	       0.7      |       0.3       |    69,637,211    |
|Zstd 1.4.8 -2 -T6                |	       0.3      |       0.3       |    69,637,211    |
|**Kanzi -l 1**                   |  	   **1.7** 	  |     **0.9**     |  **68,471,355**  |
|**Kanzi -l 1 -j 6**              |  	   **0.7** 	  |     **0.3**     |  **68,471,355**  |
|Pigz 1.6 -6 -p6                  |        1.4      |       1.4       |    68,237,849    |        
|Gzip 1.6 -6                      |        6.1      |       1.1       |    68,227,965    |   
|Brotli 1.0.9 -2 --large_window=30|        1.5      |       0.8       |    68,033,377    |
|Pigz 1.6 -9 -p6                  |        3.0      |       1.6       |    67,656,836    |
|Gzip 1.6 -9                      |       14.0      |       1.0       |    67,631,990    |        
|**Kanzi -l 2**                   |	     **2.4**	  |     **0.9**     |  **64,522,501**  |
|**Kanzi -l 2 -j 6**              |	     **0.9**	  |     **0.3**     |  **64,522,501**  |
|Brotli 1.0.9 -4 --large_window=30|        4.1      |       0.7       |    64,267,169    |
|Zstd 1.4.8 -9                    |	       4.9      |       0.3       |    60,229,401    |
|Zstd 1.4.8 -9  -T6               |	       3.2      |       0.3       |    60,229,401    |
|**Kanzi -l 3**                   |	     **3.8**	  |     **1.9**     |  **59,647,212**  |
|**Kanzi -l 3 -j 6**              |	     **1.2**	  |     **0.6**     |  **59,647,212**  |
|Zstd 1.4.8 -13                   |	      16.0      |       0.3       |    58,127,009    |
|Zstd 1.4.8 -13 -T6               |	      11.4      |       0.3       |    58,127,009    |
|Orz 1.5.0                        |	       7.7      |       2.0       |    57,564,831    |
|Brotli 1.0.9 -9 --large_window=30|       36.7      |       0.7       |    56,232,817    |
|Lzma 5.2.2 -3	                  |       24.1	    |       2.6       |    55,743,540    |
|**Kanzi -l 4**                   |	     **8.0**	  |     **4.7**     |  **54,996,858**  |
|**Kanzi -l 4 -j 6**              |	     **3.0**	  |     **1.5**     |  **54,996,858**  |
|Bzip2 1.0.6 -9	                  |       14.9      |       5.2       |    54,506,769	   |
|Zstd 1.4.8 -19	                  |       60.2      |       0.3       |    53,262,435    |
|Zstd 1.4.8 -19	-T6               |       51.3      |       0.3       |    53,262,435    |
|**Kanzi -l 5**                   |	    **12.3**	  |     **6.6**     |  **51,745,795**  |
|**Kanzi -l 5 -j 6**              |      **4.4**    |     **2.0**     |  **51,739,977**  |
|Brotli 1.0.9 --large_window=30   |      356.2	    |       0.9       |    49,383,136    |
|Lzma 5.2.2 -9                    |       65.6	    |       2.5       |    48,780,457    |
|**Kanzi -l 6**	                  |     **14.6**    |     **8.7**     |  **48,067,846**  |
|**Kanzi -l 6 -j 6**              |      **5.0**    |     **2.6**     |  **48,067,846**  |
|BCM 1.6.0 -7	                    |       18.0      |      22.1       |    46,506,716    |
|**Kanzi -l 7**                   |     **18.7**	  |    **13.9**     |  **46,446,991**  |
|**Kanzi -l 7 -j 6**              |      **6.6**	  |     **5.2**     |  **46,446,991**  |
|Tangelo 2.4	                    |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1                 |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12                |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 8**                   |     **49.6**	  |    **50.0**     |  **41,830,871**  |
|**Kanzi -l 8 -j 6**              |     **17.3**	  |    **15.6**     |  **41,830,871**  |
|Tangelo 2.0	                    |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 9**                   |     **74.6**	  |    **76.7**     |  **40,369,883**  |
|**Kanzi -l 9 -j 6**              |     **26.3**	  |    **26.5**     |  **40,369,883**  |
|zpaq v7.14 m5 t1                 |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12                |	     344.3	    |     350.4       |    39,112,924    |



enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 20.04

g++ version 9.3.0

Kanzi version 1.9 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |	
|**Kanzi -l 1**               |  	  **1.21** 	  |    **0.62**     |  **32,650,127**  |
|**Kanzi -l 2**               |     **1.55**    |    **0.62**     |  **31,018,886**  |
|**Kanzi -l 3**               |     **2.17**    |    **1.08**     |  **27,328,8092** |
|**Kanzi -l 4**               |	    **3.78**    |    **2.46**     |  **25,670,935**  |
|**Kanzi -l 5**               |	    **5.50**	  |    **2.84**     |  **22,484,700**  |
|**Kanzi -l 6**               |	    **7.53**	  |    **4.05**     |  **21,232,218**  |
|**Kanzi -l 7**               |	    **9.83**	  |    **6.41**     |  **20,935,522**  |
|**Kanzi -l 8**               |	   **18.88**	  |   **19.09**     |  **19,671,830**  |
|**Kanzi -l 9**               |	   **27.70**	  |   **28.43**     |  **19,097,962**  |


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
targets. Build successfully on Ubuntu with g++ 8.4.0 and g++ 9.3.0, clang++ 10.0.0
and icc 19.0.0.117. Multithreading is supported with g++ version 5.0.0 or newer.
Compiled successfully with C++11, C++14, C++17.

### BSD
The makefile uses the gnu-make syntax. First, make sure gmake is present (or install it: 'pkg_add gmake').
Go to the source directory and run 'gmake clean && gmake'. The Makefile contains all the necessary
targets.
