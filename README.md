kanzi
=====


Lossless data compression in C++.
The goal is to provide clean APIs and really fast implementation.
It includes compression codecs (Run Length coding, Exp Golomb coding, Huffman, Range, LZ4, Snappy, ANS, Context Mixers, PAQ derivatives), bit stream manipulation, and transforms such as Burrows-Wheeler (BWT) and Move-To-Front, etc ...



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



Silesia corpus benchmark
-------------------------

i7-7700K @4.20GHz, 32GB RAM, Ubuntu 18.04

g++ version 7.3.0

Kanzi version 1.4 C++ implementation. Block size is 100 MB. 


|        Compressor           | Encoding (sec)  | Decoding (sec)  |    Size          |
|-----------------------------|-----------------|-----------------|------------------|
|Original     	              |                 |                 |   211,938,580    |	
|**Kanzi -l 1**               |  	   **2.1** 	  |     **1.1**     |  **80,790,093**  |
|**Kanzi -l 1 -j 12**         |  	   **0.8** 	  |     **0.5**     |  **80,790,093**  |
|Gzip 1.6	                    |        6.0      |       1.0       |    68,227,965    |        
|Gzip 1.6	-9                  |       14.3      |       1.0       |    67,631,990    |        
|Zstd 1.3.3 -13               |	      11.9      |       0.3       |    58,789,182    |
|Lzma 5.2.2 -3	              |       24.3	    |       2.4       |    55,743,540    |
|**Kanzi -l 2**               |	     **9.3**	  |     **6.3**     |  **55,728,473**  |
|**Kanzi -l 2 -j 12**         |	     **3.6**	  |     **2.3**     |  **55,728,473**  |
|Bzip2 1.0.6 -9	              |       14.1      |       4.8       |    54,506,769	   |
|Zstd 1.3.3 -19	              |       45.2      |       0.4       |    53,977,895    |
|**Kanzi -l 3**               |	    **14.0**	  |    **12.4**     |  **51,781,285**  |
|**Kanzi -l 3 -j 12**         |      **4.7**    |     **3.9**     |  **51,781,285**  |
|**Kanzi -l 4**	              |     **16.6**    |    **16.0**     |  **49,460,294**  |
|**Kanzi -l 4 -j 12**         |      **5.7**    |     **5.3**     |  **49,460,294**  |
|Lzma 5.2.2 -9                |       65.0	    |       2.4       |    48,780,457    |
|Tangelo 2.4	                |       83.2      |      85.9       |    44,862,127    |
|**Kanzi -l 5**               |     **27.0**	  |    **25.8**     |  **46,485,064**  |
|**Kanzi -l 5 -j 12**         |      **8.3**	  |     **8.2**     |  **46,485,064**  |
|zpaq v7.14 m4 t1             |      107.3	    |     112.2       |    42,628,166    |
|zpaq v7.14 m4 t12            |      108.1	    |     111.5       |    42,628,166    |
|Tangelo 2.0	                |      302.0    	|     310.9       |    41,267,068    |
|**Kanzi -l 6**               |     **74.5**	  |    **75.4**     |  **41,144,431**  |
|**Kanzi -l 6 -j 12**         |     **28.5**	  |    **28.9**     |  **41,144,431**  |
|zpaq v7.14 m5 t1             |	     343.1	    |     352.0       |    39,112,924    |
|zpaq v7.14 m5 t12            |	     344.3	    |     350.4       |    39,112,924    |
