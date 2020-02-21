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
Jarek Duda, 
Ilya Grebnov

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

Kanzi version 1.7 C++ implementation. Block size is 100 MB. 


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   211,938,580    |	
|**Kanzi -l 1**               |  	   **1.6** 	  |     **0.9**     |  **74,599,655**  |
|**Kanzi -l 1 -j 6**          |  	   **0.7** 	  |     **0.4**     |  **74,599,655**  |
|Gzip 1.6	                    |        6.0      |       1.0       |    68,227,965    |        
|Gzip 1.6	-9                  |       14.3      |       1.0       |    67,631,990    |        
|**Kanzi -l 2**               |	     **3.6**	  |     **1.8**     |  **61,679,732**  |
|**Kanzi -l 2 -j 6**          |	     **1.3**	  |     **0.6**     |  **61,679,732**  |
|Zstd 1.4.5 -13               |	      15.9      |       0.3       |    58,125,865    |
|Orz 1.5.0                    |	       7.7      |       1.9       |    57,564,831    |
|Brotli 1.0.7 -9              |       91.9      |       1.4       |    56,289,305    |
|**Kanzi -l 3**               |	     **7.4**	  |     **4.8**     |  **55,952,061**  |
|**Kanzi -l 3 -j 6**          |	     **2.5**	  |     **1.5**     |  **55,952,061**  |
|Lzma 5.2.2 -3	              |       24.3	    |       2.4       |    55,743,540    |
|Bzip2 1.0.6 -9	              |       14.1      |       4.8       |    54,506,769	   |
|Zstd 1.4.5 -19	              |       45.2      |       0.3       |    53,261,006    |
|**Kanzi -l 4**               |	    **12.2**	  |     **7.1**     |  **51,754,417**  |
|**Kanzi -l 4 -j 6**          |      **4.2**    |     **2.3**     |  **51,754,417**  |
|Lzma 5.2.2 -9                |       65.0	    |       2.4       |    48,780,457    |
|**Kanzi -l 5**	              |     **15.7**    |    **10.1**     |  **48,256,346**  |
|**Kanzi -l 5 -j 6**          |      **5.5**    |     **3.7**     |  **48,256,346**  |
|**Kanzi -l 6**               |     **21.7**	  |    **18.3**     |  **46,394,304**  |
|**Kanzi -l 6 -j 6**          |      **7.1**	  |     **6.4**     |  **46,394,304**  |
|Tangelo 2.4	                |       83.2      |      85.9       |    44,862,127    |
|zpaq v7.14 m4 t1             |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12            |      108.1	    |     111.5       |    42,628,166    |
|**Kanzi -l 7**               |     **47.9**	  |    **49.9**     |  **41,862,443**  |
|**Kanzi -l 7 -j 6**          |     **16.6**	  |    **17.7**     |  **41,862,443**  |
|Tangelo 2.0	                |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 8**               |     **69.9**	  |    **72.2**     |  **40,473,911**  |
|**Kanzi -l 8 -j 6**          |     **27.7**	  |    **26.1**     |  **40,473,911**  |
|zpaq v7.14 m5 t1             |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12            |	     344.3	    |     350.4       |    39,112,924    |


enwik8
-------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 8.3.0

Kanzi version 1.7 C++ implementation. Block size is 100 MB. 1 thread


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   100,000,000    |	
|**Kanzi -l 1**               |  	  **1.27** 	  |    **0.73**     |  **33,869,944**  |
|**Kanzi -l 2**               |     **2.08**    |    **1.20**     |  **27,404,489**  |        
|**Kanzi -l 3**               |	    **3.85**    |    **2.56**     |  **25,661,699**  |
|**Kanzi -l 4**               |	    **5.51**	  |    **2.99**     |  **22,478,636**  |
|**Kanzi -l 5**               |	    **7.95**	  |    **4.70**     |  **21,275,446**  |
|**Kanzi -l 6**               |	   **10.08**	  |    **7.50**     |  **20,869,366**  |
|**Kanzi -l 7**               |	   **19.01**	  |   **19.58**     |  **19,570,938**  |
|**Kanzi -l 8**               |	   **26.17**	  |   **27.99**     |  **19,141,858**  |


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
Compiled successfully against C++03, C++11, C++14, C++17.
