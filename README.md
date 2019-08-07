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
<a href="https://scan.coverity.com/projects/flanglet-kanzi-cpp">
  <img alt="Coverity Scan Build Status"
       src="https://img.shields.io/coverity/scan/16859.svg"/>
</a>

Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 8.3.0

Kanzi version 1.6 C++ implementation. Block size is 100 MB. 


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   211,938,580    |	
|**Kanzi -l 1**               |  	   **1.5** 	  |     **0.9**     |  **76,600,331**  |
|**Kanzi -l 1 -j 6**          |  	   **0.6** 	  |     **0.4**     |  **76,600,331**  |
|Gzip 1.6	                    |        6.0      |       1.0       |    68,227,965    |        
|Gzip 1.6	-9                  |       14.3      |       1.0       |    67,631,990    |        
|**Kanzi -l 2**               |	     **3.8**	  |     **2.0**     |  **61,788,747**  |
|**Kanzi -l 2 -j 6**          |	     **1.4**	  |     **0.7**     |  **61,788,747**  |
|Zstd 1.3.3 -13               |	      11.9      |       0.3       |    58,789,182    |
|Brotli 1.0.5 -9              |       94.3      |       1.4       |    56,289,305    |
|**Kanzi -l 3**               |	     **7.5**	  |     **4.7**     |  **55,983,177**  |
|**Kanzi -l 3 -j 6**          |	     **2.9**	  |     **1.6**     |  **55,983,177**  |
|Lzma 5.2.2 -3	              |       24.3	    |       2.4       |    55,743,540    |
|Bzip2 1.0.6 -9	              |       14.1      |       4.8       |    54,506,769	   |
|Zstd 1.3.3 -19	              |       45.2      |       0.4       |    53,977,895    |
|**Kanzi -l 4**               |	    **12.2**	  |     **7.2**     |  **51,800,821**  |
|**Kanzi -l 4 -j 6**          |      **4.2**    |     **2.4**     |  **51,800,821**  |
|Lzma 5.2.2 -9                |       65.0	    |       2.4       |    48,780,457    |
|**Kanzi -l 5**	              |     **15.7**    |    **10.0**     |  **48,279,102**  |
|**Kanzi -l 5 -j 6**          |      **5.5**    |     **3.2**     |  **48,279,102**  |
|**Kanzi -l 6**               |     **24.8**	  |    **20.8**     |  **46,485,189**  |
|**Kanzi -l 6 -j 6**          |      **8.5**	  |     **6.4**     |  **46,485,189**  |
|Tangelo 2.4	                |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1             |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12            |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 7**               |     **50.9**	  |    **51.9**     |  **41,892,099**  |
|**Kanzi -l 7 -j 6**          |     **18.0**	  |    **18.4**     |  **41,892,099**  |
|Tangelo 2.0	                |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 8**               |     **73.7**	  |    **75.4**     |  **40,502,391**  |
|**Kanzi -l 8 -j 6**          |     **27.8**	  |    **29.1**     |  **40,502,391**  |
|zpaq v7.14 m5 t1             |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12            |	     344.3	    |     350.4       |    39,112,924    |


enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 8.3.0

Kanzi version 1.6 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |	
|**Kanzi -l 1**               |  	  **1.14** 	  |    **0.66**     |  **34,135,723**  |
|**Kanzi -l 2**               |     **2.10**    |    **1.17**     |  **27,450,033**  |        
|**Kanzi -l 3**               |	    **3.85**    |    **2.43**     |  **25,695,567**  |
|**Kanzi -l 4**               |	    **5.38**	  |    **3.05**     |  **22,512,452**  |
|**Kanzi -l 5**               |	    **7.75**	  |    **4.66**     |  **21,301,346**  |
|**Kanzi -l 6**               |	   **14.36**	  |   **10.26**     |  **20,791,496**  |
|**Kanzi -l 7**               |	   **19.45**	  |   **19.48**     |  **19,597,394**  |
|**Kanzi -l 8**               |	   **27.04**	  |   **27.76**     |  **19,163,098**  |


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
