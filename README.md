kanzi
=====


State-of-the-art lossless data compression in C++.
The goal is to provide clean APIs and really fast implementation.
It includes compression codecs (Run Length coding, Exp Golomb coding, Huffman, Range, LZ, ANS, Context Mixers, PAQ derivatives), bit stream manipulation, and transforms such as Burrows-Wheeler (BWT) and Move-To-Front, etc ...



For more details, check https://github.com/flanglet/kanzi/wiki.

Credits

Matt Mahoney,
Yann Collet,
Jan Ondrus,
Yuta Mori,
Ilya Muravyov,
Neal Burns,
Fabian Giesen,
Jarek Duda

Disclaimer

Use at your own risk. Always keep a backup of your files.

[![Build Status](https://travis-ci.org/flanglet/kanzi-cpp.svg?branch=master)](https://travis-ci.org/flanglet/kanzi-cpp)


Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 7.3.0

Kanzi version 1.5 C++ implementation. Block size is 100 MB. 


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   211,938,580    |	
|**Kanzi -l 1**               |  	   **1.6** 	  |     **1.1**     |  **80,003,837**  |
|**Kanzi -l 1 -j 12**         |  	   **0.6** 	  |     **0.4**     |  **80,003,837**  |
|Gzip 1.6	                    |        6.0      |       1.0       |    68,227,965    |        
|Gzip 1.6	-9                  |       14.3      |       1.0       |    67,631,990    |        
|**Kanzi -l 2**               |	     **4.0**	  |     **2.7**     |  **63,878,466**  |
|**Kanzi -l 2 -j 12**         |	     **1.5**	  |     **1.0**     |  **63,878,466**  |
|Zstd 1.3.3 -13               |	      11.9      |       0.3       |    58,789,182    |
|Brotli 1.0.5 -9              |       94.3      |       1.4       |    56,289,305    |
|Lzma 5.2.2 -3	              |       24.3	    |       2.4       |    55,743,540    |
|**Kanzi -l 3**               |	     **8.4**	  |     **5.8**     |  **55,594,153**  |
|**Kanzi -l 3 -j 12**         |	     **3.3**	  |     **2.0**     |  **55,594,153**  |
|Bzip2 1.0.6 -9	              |       14.1      |       4.8       |    54,506,769	   |
|Zstd 1.3.3 -19	              |       45.2      |       0.4       |    53,977,895    |
|**Kanzi -l 4**               |	    **12.2**	  |    **10.8**     |  **51,795,306**  |
|**Kanzi -l 4 -j 12**         |      **4.4**    |     **3.5**     |  **51,795,306**  |
|**Kanzi -l 5**	              |     **15.6**    |    **15.5**     |  **49,455,342**  |
|**Kanzi -l 5 -j 12**         |      **5.5**    |     **5.0**     |  **49,455,342**  |
|Lzma 5.2.2 -9                |       65.0	    |       2.4       |    48,780,457    |
|**Kanzi -l 6**               |     **24.9**	  |    **24.7**     |  **46,485,165**  |
|**Kanzi -l 6 -j 12**         |      **7.7**	  |     **7.6**     |  **46,485,165**  |
|Tangelo 2.4	                |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1             |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12            |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 7**               |     **56.5**	  |    **57.5**     |  **41,838,503**  |
|**Kanzi -l 7 -j 12**         |     **22.0**	  |    **21.8**     |  **41,838,503**  |
|Tangelo 2.0	                |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 8**               |     **79.0**	  |    **79.0**     |  **40,844,691**  |
|**Kanzi -l 8 -j 12**         |     **31.0**	  |    **30.8**     |  **40,844,691**  |
|zpaq v7.14 m5 t1             |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12            |	     344.3	    |     350.4       |    39,112,924    |


enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 7.3.0

Kanzi version 1.5 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |	
|**Kanzi -l 1**               |  	  **1.21** 	  |    **0.66**     |  **35,611,290**  |
|**Kanzi -l 2**               |     **2.23**    |    **1.63**     |  **28,468,601**  |        
|**Kanzi -l 3**               |	    **4.21**    |    **2.96**     |  **25,517,555**  |
|**Kanzi -l 4**               |	    **5.44**	  |    **4.15**     |  **22,512,813**  |
|**Kanzi -l 5**               |	    **7.62**	  |    **6.12**     |  **21,934,022**  |
|**Kanzi -l 6**               |	   **14.69**	  |   **12.72**     |  **20,791,492**  |
|**Kanzi -l 7**               |	   **22.48**	  |   **22.44**     |  **19,613,190**  |
|**Kanzi -l 8**               |	   **29.53**	  |   **29.72**     |  **19,284,434**  |


Build Kanzi
-----------

The C++ code can be built on Windows with Visual Studio and Linux with g++ and clang++.
Porting to other operating systems should be straightforward.

### Visual Studio 2008
Unzip the file "Kanzi_VS2008.zip" in place.
The project generates a Windows 32 binary. Multithreading is not supported with this version.

### Visual Studio 2017
Unzip the file "Kanzi_VS2017.zip" in place.
The project generates a Windows 64 binary. Multithreading is supported with this version.

### mingw-w64
Go to the source directory and run 'mingw32-make.exe'. The Makefile contains all the necessary
targets. Tested successfully on Win64 with mingw-w64 g++ 8.1.0. Multithreading is supported.

### Linux
Go to the source directory and run 'make clean && make'. The Makefile contains all the necessary
targets. Build successfully on Ubuntu 18.04 with g++ 7.4.0 and g++ 8.3.0, clang++ 6.0.0
and icc 19.0.0.117. Multithreading is supported with g++ version 5.0.0 or newer.
Compiled successfully against C++03, C++11, C++14.
