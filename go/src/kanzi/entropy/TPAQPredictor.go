/*
Copyright 2011-2013 Frederic Langlet
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
you may obtain a copy of the License at

                http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package entropy

import (
	"kanzi"
)

// Tangelo PAQ predictor
// Derived from a modified version of Tangelo 2.4
// See http://encode.ru/threads/1738-TANGELO-new-compressor-(derived-from-PAQ8-FP8)

const (
	TPAQ_MAX_LENGTH = 160
	TPAQ_MIXER_SIZE = 0x1000
	TPAQ_HASH_SIZE  = 8 * 1024 * 1024
	TPAQ_MASK0      = TPAQ_MIXER_SIZE - 1
	TPAQ_MASK1      = TPAQ_HASH_SIZE - 1
	TPAQ_MASK2      = 8*TPAQ_HASH_SIZE - 1
	TPAQ_MASK3      = 32*TPAQ_HASH_SIZE - 1
	TPAQ_C1         = int32(-862048943)
	TPAQ_C2         = int32(461845907)
	TPAQ_C3         = int32(-430675100)
	TPAQ_C4         = int32(-2048144789)
	TPAQ_C5         = int32(-1028477387)
	TPAQ_HASH1      = int32(200002979)
	TPAQ_HASH2      = int32(30005491)
	TPAQ_HASH3      = int32(50004239)
)

///////////////////////// state table ////////////////////////
// STATE_TABLE[state,0] = next state if bit is 0, 0 <= state < 256
// STATE_TABLE[state,1] = next state if bit is 1
// STATE_TABLE[state,2] = number of zeros in bit history represented by state
// STATE_TABLE[state,3] = number of ones represented
// States represent a bit history within some context.
// State 0 is the starting state (no bits seen).
// States 1-30 represent all possible sequences of 1-4 bits.
// States 31-252 represent a pair of counts, (n0,n1), the number
//   of 0 and 1 bits respectively.  If n0+n1 < 16 then there are
//   two states for each pair, depending on if a 0 or 1 was the last
//   bit seen.
// If n0 and n1 are too large, then there is no state to represent this
// pair, so another state with about the same ratio of n0/n1 is substituted.
// Also, when a bit is observed and the count of the opposite bit is large,
// then part of this count is discarded to favor newer data over old.
var TPAQ_STATE_TABLE = []uint8{
	1, 2, 3, 163, 143, 169, 4, 163, 5, 165,
	6, 89, 7, 245, 8, 217, 9, 245, 10, 245,
	11, 233, 12, 244, 13, 227, 14, 74, 15, 221,
	16, 221, 17, 218, 18, 226, 19, 243, 20, 218,
	21, 238, 22, 242, 23, 74, 24, 238, 25, 241,
	26, 240, 27, 239, 28, 224, 29, 225, 30, 221,
	31, 232, 32, 72, 33, 224, 34, 228, 35, 223,
	36, 225, 37, 238, 38, 73, 39, 167, 40, 76,
	41, 237, 42, 234, 43, 231, 44, 72, 45, 31,
	46, 63, 47, 225, 48, 237, 49, 236, 50, 235,
	51, 53, 52, 234, 47, 53, 54, 234, 55, 229,
	56, 219, 57, 229, 58, 233, 59, 232, 60, 228,
	61, 226, 62, 72, 63, 74, 64, 222, 65, 75,
	66, 220, 67, 167, 68, 57, 69, 218, 6, 70,
	71, 168, 71, 72, 71, 73, 61, 74, 75, 217,
	56, 76, 77, 167, 78, 79, 77, 79, 80, 166,
	81, 162, 82, 162, 83, 162, 84, 162, 85, 165,
	86, 89, 87, 89, 88, 165, 77, 89, 90, 162,
	91, 93, 92, 93, 80, 93, 94, 161, 95, 100,
	96, 93, 97, 93, 98, 93, 99, 93, 90, 93,
	101, 161, 94, 102, 103, 120, 101, 104, 102, 105,
	104, 106, 107, 108, 104, 106, 105, 109, 108, 110,
	111, 160, 112, 134, 113, 108, 114, 108, 115, 126,
	116, 117, 92, 117, 118, 121, 94, 119, 103, 120,
	119, 107, 122, 124, 123, 117, 94, 117, 113, 125,
	126, 127, 113, 124, 128, 139, 129, 130, 114, 124,
	131, 133, 132, 109, 112, 110, 134, 135, 111, 110,
	134, 136, 110, 137, 134, 138, 134, 127, 128, 140,
	128, 141, 142, 145, 143, 144, 115, 124, 113, 125,
	142, 146, 128, 147, 148, 151, 149, 125, 79, 150,
	148, 127, 142, 152, 148, 153, 150, 154, 155, 156,
	149, 139, 157, 158, 149, 139, 159, 156, 149, 139,
	131, 130, 101, 117, 98, 163, 115, 164, 114, 141,
	91, 163, 79, 147, 58, 2, 1, 2, 170, 199,
	129, 171, 128, 172, 110, 173, 174, 177, 128, 175,
	176, 171, 129, 171, 174, 178, 179, 180, 174, 172,
	176, 181, 141, 182, 157, 183, 179, 184, 185, 186,
	157, 178, 187, 189, 188, 181, 168, 181, 151, 190,
	191, 193, 192, 182, 188, 182, 187, 194, 172, 195,
	175, 196, 170, 197, 152, 198, 185, 169, 170, 200,
	176, 201, 170, 202, 203, 204, 148, 180, 185, 205,
	203, 206, 185, 207, 192, 208, 209, 210, 188, 194,
	211, 212, 192, 184, 213, 215, 214, 193, 188, 184,
	216, 208, 168, 193, 84, 163, 54, 219, 54, 168,
	221, 94, 54, 217, 55, 223, 85, 224, 69, 225,
	63, 76, 56, 227, 86, 217, 58, 229, 230, 219,
	231, 79, 57, 86, 229, 165, 56, 217, 224, 214,
	54, 225, 54, 216, 66, 216, 58, 234, 54, 75,
	61, 214, 57, 237, 222, 74, 78, 74, 85, 163,
	82, 217, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0,
}

// State Map
var TPAQ_STATE_MAP = []int{
	-119, -120, 169, -476, -484, -386, -737, -881, -874, -712,
	-848, -679, -559, -794, -1212, -782, -1205, -1205, -613, -753,
	-1169, -1169, -1169, -743, -1155, -732, -720, -1131, -1131, -1131,
	-1131, -1131, -1131, -1131, -1131, -1131, -540, -1108, -1108, -1108,
	-1108, -1108, -1108, -1108, -1108, -1108, -1108, -2047, -2047, -2047,
	-2047, -2047, -2047, -782, -569, -389, -640, -720, -568, -432,
	-379, -640, -459, -590, -1003, -569, -981, -981, -981, -609,
	416, -1648, -245, -416, -152, -152, 416, -1017, -1017, -179,
	-424, -446, -461, -508, -473, -492, -501, -520, -528, -54,
	-395, -405, -404, -94, -232, -274, -288, -319, -354, -379,
	-105, -141, -63, -113, -18, -39, -94, 52, 103, 167,
	222, 130, -78, -135, -253, -321, -343, 102, -165, 157,
	-229, 155, -108, -188, 262, 283, 56, 447, 6, -92,
	242, 172, 38, 304, 141, 285, 285, 320, 319, 462,
	497, 447, -56, -46, 374, 485, 510, 479, -71, 198,
	475, 549, 559, 584, 586, -196, 712, -185, 673, -161,
	237, -63, 48, 127, 248, -34, -18, 416, -99, 189,
	-50, 39, 337, 263, 660, 153, 569, 832, 220, 1,
	318, 246, 660, 660, 732, 416, 732, 1, -660, 246,
	660, 1, -416, 732, 262, 832, 369, 781, 781, 324,
	1104, 398, 626, -416, 609, 1018, 1018, 1018, 1648, 732,
	1856, 1, 1856, 416, -569, 1984, -732, -164, 416, 153,
	-416, -569, -416, 1, -660, 1, -660, 153, 152, -832,
	-832, -832, -569, 0, -95, -660, 1, 569, 153, 416,
	-416, 1, 1, -569, 1, -318, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-10, -436, 401, -521, -623, -689, -736, -812, -812, -900,
	-865, -891, -1006, -965, -981, -916, -946, -976, -1072, -1014,
	-1058, -1090, -1044, -1030, -1044, -1104, -1009, -1418, -1131, -1131,
	-1269, -1332, -1191, -1169, -1108, -1378, -1367, -1126, -1297, -1085,
	-1355, -1344, -1169, -1269, -1440, -1262, -1332, -2047, -2047, -1984,
	-2047, -2047, -2047, -225, -402, -556, -502, -746, -609, -647,
	-625, -718, -700, -805, -748, -935, -838, -1053, -787, -806,
	-269, -1006, -278, -212, -41, -399, 137, -984, -998, -219,
	-455, -524, -556, -564, -577, -592, -610, -690, -650, -140,
	-396, -471, -450, -168, -215, -301, -325, -364, -315, -401,
	-96, -174, -102, -146, -61, -9, 54, 81, 116, 140,
	192, 115, -41, -93, -183, -277, -365, 104, -134, 37,
	-80, 181, -111, -184, 194, 317, 63, 394, 105, -92,
	299, 166, -17, 333, 131, 386, 403, 450, 499, 480,
	493, 504, 89, -119, 333, 558, 568, 501, -7, -151,
	203, 557, 595, 603, 650, 104, 960, 204, 933, 239,
	247, -12, -105, 94, 222, -139, 40, 168, -203, 566,
	-53, 243, 344, 542, 42, 208, 14, 474, 529, 82,
	513, 504, 570, 616, 644, 92, 669, 91, -179, 677,
	720, 157, -10, 687, 672, 750, 686, 830, 787, 683,
	723, 780, 783, 9, 842, 816, 885, 901, 1368, 188,
	1356, 178, 1419, 173, -22, 1256, 240, 167, 1, -31,
	-165, 70, -493, -45, -354, -25, -142, 98, -17, -158,
	-355, -448, -142, -67, -76, -310, -324, -225, -96, 0,
	46, -72, 0, -439, 14, -55, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-32, -521, 485, -627, -724, -752, -815, -886, -1017, -962,
	-1022, -984, -1099, -1062, -1090, -1062, -1108, -1085, -1248, -1126,
	-1233, -1104, -1233, -1212, -1285, -1184, -1162, -1309, -1240, -1309,
	-1219, -1390, -1332, -1320, -1262, -1320, -1332, -1320, -1344, -1482,
	-1367, -1355, -1504, -1390, -1482, -1482, -1525, -2047, -2047, -1984,
	-2047, -2047, -1984, -251, -507, -480, -524, -596, -608, -658,
	-713, -812, -700, -653, -820, -820, -752, -831, -957, -690,
	-402, -689, -189, -28, -13, -312, 119, -930, -973, -212,
	-459, -523, -513, -584, -545, -593, -628, -631, -688, -33,
	-437, -414, -458, -167, -301, -308, -407, -289, -389, -332,
	-55, -233, -115, -144, -100, -20, 106, 59, 130, 200,
	237, 36, -114, -131, -232, -296, -371, 140, -168, 0,
	-16, 199, -125, -182, 238, 310, 29, 423, 41, -176,
	317, 96, -14, 377, 123, 446, 458, 510, 496, 463,
	515, 471, -11, -182, 268, 527, 569, 553, -58, -146,
	168, 580, 602, 604, 651, 87, 990, 95, 977, 185,
	315, 82, -25, 140, 286, -57, 85, 14, -210, 630,
	-88, 290, 328, 422, -20, 271, -23, 478, 548, 64,
	480, 540, 591, 601, 583, 26, 696, 117, -201, 740,
	717, 213, -22, 566, 599, 716, 709, 764, 740, 707,
	790, 871, 925, 3, 969, 990, 990, 1023, 1333, 154,
	1440, 89, 1368, 125, -78, 1403, 128, 100, -88, -20,
	-250, -140, -164, -14, -175, -6, -13, -23, -251, -195,
	-422, -419, -107, -89, -24, -69, -244, -51, -27, -250,
	0, 1, -145, 74, 12, 11, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-25, -605, 564, -746, -874, -905, -949, -1044, -1126, -1049,
	-1099, -1140, -1248, -1122, -1184, -1240, -1198, -1285, -1262, -1332,
	-1418, -1402, -1390, -1285, -1418, -1418, -1418, -1367, -1552, -1440,
	-1367, -1584, -1344, -1616, -1344, -1390, -1418, -1461, -1616, -1770,
	-1648, -1856, -1770, -1584, -1648, -2047, -1685, -2047, -2047, -1856,
	-2047, -2047, -1770, -400, -584, -523, -580, -604, -625, -587,
	-739, -626, -774, -857, -737, -839, -656, -888, -984, -624,
	-26, -745, -211, -103, -73, -328, 142, -1072, -1062, -231,
	-458, -494, -518, -579, -550, -541, -653, -621, -703, -53,
	-382, -444, -417, -199, -288, -367, -273, -450, -268, -477,
	-101, -157, -123, -156, -107, -9, 71, 64, 133, 174,
	240, 25, -138, -127, -233, -272, -383, 105, -144, 85,
	-115, 188, -112, -245, 236, 305, 26, 395, -3, -164,
	321, 57, -68, 346, 86, 448, 482, 541, 515, 461,
	503, 454, -22, -191, 262, 485, 557, 550, -53, -152,
	213, 565, 570, 649, 640, 122, 931, 92, 990, 172,
	317, 54, -12, 127, 253, 8, 108, 104, -144, 733,
	-64, 265, 370, 485, 152, 366, -12, 507, 473, 146,
	462, 579, 549, 659, 724, 94, 679, 72, -152, 690,
	698, 378, -11, 592, 652, 764, 730, 851, 909, 837,
	896, 928, 1050, 74, 1095, 1077, 1206, 1059, 1403, 254,
	1552, 181, 1552, 238, -31, 1526, 199, 47, -214, 32,
	-219, -153, -323, -198, -319, -108, -107, -90, -177, -210,
	-184, -455, -216, -19, -107, -219, -22, -232, -19, -198,
	-198, -113, -398, 0, -49, -29, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-34, -648, 644, -793, -889, -981, -1053, -1108, -1108, -1117,
	-1176, -1198, -1205, -1140, -1355, -1332, -1418, -1440, -1402, -1355,
	-1367, -1418, -1402, -1525, -1504, -1402, -1390, -1378, -1525, -1440,
	-1770, -1552, -1378, -1390, -1616, -1648, -1482, -1616, -1390, -1728,
	-1770, -2047, -1685, -1616, -1648, -1685, -1584, -2047, -1856, -1856,
	-2047, -2047, -2047, -92, -481, -583, -623, -602, -691, -803,
	-815, -584, -728, -743, -796, -734, -884, -728, -1616, -747,
	-416, -510, -265, 1, -44, -409, 141, -1014, -1094, -201,
	-490, -533, -537, -605, -536, -564, -676, -620, -688, -43,
	-439, -361, -455, -178, -309, -315, -396, -273, -367, -341,
	-92, -202, -138, -105, -117, -4, 107, 36, 90, 169,
	222, -14, -92, -125, -219, -268, -344, 70, -137, -49,
	4, 171, -72, -224, 210, 319, 15, 386, -2, -195,
	298, 53, -31, 339, 95, 383, 499, 557, 491, 457,
	468, 421, -53, -168, 267, 485, 573, 508, -65, -109,
	115, 568, 576, 619, 685, 179, 878, 131, 851, 175,
	286, 19, -21, 113, 245, -54, 101, 210, -121, 766,
	-47, 282, 441, 483, 129, 303, 16, 557, 460, 114,
	492, 596, 580, 557, 605, 133, 643, 154, -115, 668,
	683, 332, -44, 685, 735, 765, 757, 889, 890, 922,
	917, 1012, 1170, 116, 1104, 1192, 1199, 1213, 1368, 254,
	1462, 307, 1616, 359, 50, 1368, 237, 52, -112, -47,
	-416, -255, -101, 55, -177, -166, -73, -132, -56, -132,
	-237, -495, -152, -43, 69, 46, -121, -191, -102, 170,
	-137, -45, -364, -57, -212, 7, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-30, -722, 684, -930, -1006, -1155, -1191, -1212, -1332, -1149,
	-1276, -1297, -1320, -1285, -1344, -1648, -1402, -1482, -1552, -1255,
	-1344, -1504, -1728, -1525, -1418, -1728, -1856, -1584, -1390, -1552,
	-1552, -1984, -1482, -1525, -1856, -2047, -1525, -1770, -1648, -1770,
	-1482, -1482, -1482, -1584, -2047, -2047, -1552, -2047, -2047, -2047,
	-2047, -1984, -2047, 0, -376, -502, -568, -710, -761, -860,
	-838, -750, -1058, -897, -787, -865, -646, -844, -979, -1000,
	-416, -564, -832, -416, -64, -555, 304, -954, -1081, -219,
	-448, -543, -510, -550, -544, -564, -650, -595, -747, -61,
	-460, -404, -430, -183, -287, -315, -366, -311, -347, -328,
	-109, -240, -151, -117, -156, -32, 64, 19, 78, 116,
	223, 6, -195, -125, -204, -267, -346, 63, -125, -92,
	-22, 186, -128, -169, 182, 290, -14, 384, -27, -134,
	303, 0, -5, 328, 96, 351, 483, 459, 529, 423,
	447, 390, -104, -165, 214, 448, 588, 550, -127, -146,
	31, 552, 563, 620, 718, -50, 832, 14, 851, 93,
	281, 60, -5, 121, 257, -16, 103, 138, -184, 842,
	-21, 319, 386, 411, 107, 258, 66, 475, 542, 178,
	501, 506, 568, 685, 640, 78, 694, 122, -96, 634,
	826, 165, 220, 794, 736, 960, 746, 823, 833, 939,
	1045, 1004, 1248, 22, 1118, 1077, 1213, 1127, 1552, 241,
	1440, 282, 1483, 315, -102, 1391, 352, 124, -188, 19,
	1, -268, -782, 0, -322, 116, 46, -129, 95, -102,
	-238, -459, -262, -100, 122, -152, -455, -269, -238, 0,
	-152, -416, -369, -219, -175, -41, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-11, -533, 477, -632, -731, -815, -808, -910, -940, -995,
	-1094, -1040, -946, -1044, -1198, -1099, -1104, -1090, -1162, -1122,
	-1145, -1205, -1248, -1269, -1255, -1285, -1140, -1219, -1269, -1285,
	-1269, -1367, -1344, -1390, -1482, -1332, -1378, -1461, -1332, -1461,
	-1525, -1584, -1418, -1504, -1648, -1648, -1648, -1856, -1856, -1616,
	-1984, -1525, -2047, -330, -456, -533, -524, -541, -577, -631,
	-715, -670, -710, -729, -743, -738, -759, -775, -850, -690,
	-193, -870, -102, 21, -45, -282, 96, -1000, -984, -177,
	-475, -506, -514, -582, -597, -602, -622, -633, -695, -22,
	-422, -381, -435, -107, -290, -327, -360, -316, -366, -374,
	-62, -212, -111, -162, -83, -8, 127, 52, 101, 193,
	237, -16, -117, -150, -246, -275, -361, 122, -134, -21,
	28, 220, -132, -215, 231, 330, 40, 406, -11, -196,
	329, 68, -42, 391, 101, 396, 483, 519, 480, 464,
	516, 484, -34, -200, 269, 487, 525, 510, -79, -142,
	150, 517, 555, 594, 718, 86, 861, 102, 840, 134,
	291, 74, 10, 166, 245, 16, 117, -21, -126, 652,
	-71, 291, 355, 491, 10, 251, -21, 527, 525, 43,
	532, 531, 573, 631, 640, 31, 629, 87, -164, 680,
	755, 145, 14, 621, 647, 723, 748, 687, 821, 745,
	794, 785, 859, 23, 887, 969, 996, 1007, 1286, 104,
	1321, 138, 1321, 169, -24, 1227, 123, 116, 13, 45,
	-198, -38, -214, -22, -241, 13, -161, -54, -108, -120,
	-345, -484, -119, -80, -58, -189, -253, -223, -106, -73,
	-57, -64, -268, -208, -4, 12, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-38, -419, 362, -548, -577, -699, -725, -838, -860, -869,
	-891, -970, -989, -1030, -1014, -1030, -1169, -1067, -1113, -1155,
	-1212, -1176, -1269, -1205, -1320, -1378, -1169, -1285, -1418, -1240,
	-1320, -1332, -1402, -1390, -1285, -1402, -1262, -1240, -1616, -1320,
	-1552, -1440, -1320, -1685, -1482, -1685, -1320, -1616, -1856, -1616,
	-1856, -2047, -1728, -302, -466, -608, -475, -502, -550, -598,
	-623, -584, -716, -679, -759, -767, -579, -713, -686, -652,
	-294, -791, -240, -55, -177, -377, -108, -789, -858, -226,
	-370, -423, -449, -474, -481, -503, -541, -551, -561, -93,
	-353, -345, -358, -93, -215, -246, -295, -304, -304, -349,
	-48, -200, -90, -150, -52, -14, 92, 19, 105, 177,
	217, 28, -44, -83, -155, -199, -273, 53, -133, -7,
	26, 135, -90, -137, 177, 250, 32, 355, 55, -89,
	254, 67, -21, 318, 152, 373, 387, 413, 427, 385,
	436, 355, 41, -121, 261, 406, 470, 452, 40, -58,
	223, 474, 546, 572, 534, 184, 682, 205, 757, 263,
	276, 6, -51, 78, 186, -65, 48, -46, -18, 483,
	3, 251, 334, 444, 115, 254, 80, 480, 480, 207,
	476, 511, 570, 603, 561, 170, 583, 145, -7, 662,
	647, 287, 88, 608, 618, 713, 728, 725, 718, 520,
	599, 621, 664, 135, 703, 701, 771, 807, 903, 324,
	885, 240, 880, 296, 109, 920, 305, -24, -314, -44,
	-202, -145, -481, -379, -341, -128, -187, -179, -342, -201,
	-419, -405, -214, -150, -119, -493, -447, -133, -331, -224,
	-513, -156, -247, -108, -177, -95, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-37, -350, 295, -455, -569, -592, -653, -686, -764, -819,
	-825, -897, -954, -908, -951, -984, -987, -924, -995, -1030,
	-1081, -1019, -1022, -1058, -995, -1122, -1009, -1090, -1085, -1191,
	-1094, -1000, -1026, -1248, -1162, -1285, -1085, -1108, -1017, -1219,
	-1126, -1026, -976, -1320, -1320, -1584, -1176, -2047, -1728, -2047,
	-1685, -2047, -2047, -281, -492, -568, -551, -564, -636, -701,
	-736, -690, -667, -831, -841, -806, -897, -888, -881, -891,
	-337, -884, -120, -123, -143, -359, -15, -910, -981, -205,
	-440, -499, -526, -549, -533, -614, -591, -653, -690, -124,
	-423, -445, -405, -125, -246, -285, -297, -345, -303, -378,
	-95, -189, -96, -131, -66, 2, 63, 49, 115, 146,
	223, 82, -60, -96, -204, -248, -326, 90, -142, 34,
	-33, 157, -125, -178, 234, 296, 48, 383, 90, -69,
	333, 139, 5, 369, 152, 398, 419, 426, 467, 455,
	528, 475, 61, -139, 316, 502, 560, 502, 45, -102,
	213, 537, 596, 606, 622, 159, 820, 222, 813, 247,
	254, 16, -45, 88, 214, -73, 18, -73, -90, 450,
	-44, 237, 349, 400, 61, 151, 3, 405, 454, 124,
	431, 414, 518, 618, 616, 95, 647, 67, -146, 593,
	697, 64, -41, 560, 589, 620, 708, 826, 723, 507,
	555, 601, 690, -35, 778, 814, 875, 894, 1248, 148,
	1333, 138, 1234, 136, -7, 1298, 88, 0, -55, -49,
	-137, -138, -159, -101, -346, -136, -214, 47, -219, -199,
	-411, -416, -187, -82, -97, -416, -241, -267, -436, -343,
	55, -273, -198, -24, -103, -90, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-104, -555, 421, -707, -754, -855, -874, -943, -970, -1030,
	-1014, -1113, -1226, -1131, -1122, -1418, -1176, -1276, -1155, -1205,
	-1367, -1378, -1482, -1332, -1320, -1685, -1482, -1440, -1191, -1552,
	-1262, -1233, -1440, -1402, -1402, -1240, -1367, -1770, -1355, -1770,
	-1344, -1344, -1176, -1053, -1145, -1131, -1276, -1616, -1344, -1525,
	-1418, -1248, -1390, -660, -337, -1122, -359, -511, -549, -1169,
	-678, -1040, -459, -660, -640, -625, -1019, -1003, -590, -1040,
	1, 1, 246, 569, 62, -337, 660, -681, -703, -179,
	-335, -459, -445, -503, -450, -529, -489, -616, -507, 2,
	-270, -234, -326, -124, -171, -222, -251, -261, -220, -387,
	-90, -166, -24, -40, -93, 28, -88, 214, 129, 119,
	182, 69, -24, -44, -133, -215, -255, 42, -123, -12,
	4, 121, -87, -49, 172, 200, 105, 315, 85, -110,
	221, 117, 18, 298, 151, 347, 286, 359, 307, 369,
	371, 328, -31, -141, 196, 432, 459, 537, 14, -215,
	370, 563, 662, 614, 683, 84, 625, 176, 652, 370,
	250, -2, 74, 114, 223, 8, 201, -152, 0, 550,
	4, 274, 270, 334, 40, 280, 122, 498, 381, 313,
	382, 388, 481, 524, 555, 213, 562, 460, 1, 521,
	449, 456, 139, 620, 671, 565, 732, 588, 868, 612,
	740, 718, 736, 114, 783, 820, 889, 988, 1001, 270,
	990, 191, 1027, 146, 304, 1010, 164, 48, -56, 896,
	-416, -213, -416, 416, -732, -95, 95, -208, 1, -416,
	100, -95, 95, 17, 124, 1, -416, 246, 220, 0,
	1, 0, 0, 46, 256, -57, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-44, -551, 478, -690, -789, -913, -935, -1006, -995, -1131,
	-1035, -1169, -1099, -1198, -1297, -1145, -1309, -1198, -1219, -1226,
	-1332, -1248, -1378, -1297, -1390, -1226, -1378, -1440, -1584, -1418,
	-1552, -1269, -1402, -1504, -1552, -1461, -1584, -1504, -1616, -1584,
	-1482, -1685, -1856, -1856, -1685, -1685, -1482, -1770, -1685, -1984,
	-2047, -1856, -1856, -405, -619, -642, -574, -582, -618, -752,
	-619, -780, -682, -680, -624, -772, -699, -935, -815, -749,
	-897, -711, -601, -99, -251, -398, -331, -846, -930, -330,
	-455, -488, -470, -572, -563, -570, -583, -615, -706, -216,
	-377, -415, -415, -166, -256, -310, -338, -397, -349, -442,
	-114, -229, -103, -168, -65, -20, 68, 26, 96, 200,
	239, 91, -3, -53, -140, -231, -335, 12, -174, -67,
	-46, 91, -44, -225, 172, 272, 74, 398, 150, -28,
	299, 116, 18, 377, 156, 465, 470, 502, 486, 439,
	471, 367, 64, -97, 319, 471, 506, 593, 194, 34,
	334, 501, 637, 649, 669, 321, 767, 291, 799, 444,
	312, -56, -173, 4, 161, -170, -226, -314, -29, 614,
	93, 322, 425, 460, 123, 341, 185, 496, 523, 250,
	506, 544, 613, 579, 619, 312, 708, 322, 184, 674,
	603, 372, 423, 691, 767, 746, 747, 792, 732, 750,
	776, 845, 1010, 319, 977, 996, 1040, 985, 1256, 381,
	1277, 458, 1256, 457, 0, 1321, 432, -226, -370, -187,
	-869, -374, -416, -69, -503, -308, -371, -403, -255, -297,
	-408, -891, -471, -243, -805, -398, -134, -569, -319, -960,
	-610, -543, -398, -231, -268, -244, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-46, -743, 732, -916, -1030, -1006, -1090, -1145, -1162, -1117,
	-1184, -1367, -1378, -1233, -1176, -1309, -1378, -1205, -1616, -1367,
	-1482, -1402, -1552, -1482, -1461, -1233, -1504, -1390, -1482, -1525,
	-1504, -1461, -1320, -1402, -1525, -1856, -1504, -1482, -1332, -1648,
	-1461, -1402, -1440, -1770, -1770, -1504, -2047, -1685, -1856, -2047,
	-2047, -2047, -2047, -247, -366, -436, -532, -534, -616, -668,
	-721, -697, -855, -627, -780, -729, -722, -848, -998, -654,
	-208, -1104, -320, -93, -79, -270, 324, -938, -1003, -176,
	-441, -492, -495, -533, -553, -546, -538, -605, -637, -63,
	-409, -404, -436, -121, -268, -317, -390, -364, -367, -367,
	-131, -231, -113, -133, -104, -4, -14, 17, 106, 146,
	191, 95, -58, -115, -216, -278, -388, 37, -174, 175,
	-295, 128, -162, -239, 281, 374, 13, 519, -41, -26,
	307, 130, 24, 345, 128, 398, 419, 468, 494, 529,
	545, 521, -107, -39, 270, 538, 576, 473, -115, 171,
	418, 511, 585, 620, 588, -125, 823, -85, 875, -88,
	266, -97, -39, 32, 191, -68, -41, 83, -266, 875,
	-11, 268, 336, 502, 114, 185, 15, 593, 528, 121,
	606, 481, 633, 733, 539, 85, 792, 245, -201, 653,
	795, 591, -97, 606, 592, 707, 794, 873, 837, 925,
	1001, 1100, 1156, 98, 1059, 1199, 1177, 1213, 1504, 157,
	1526, 183, 1526, 70, -29, 1419, 180, 104, -109, -38,
	-219, -150, -255, -169, -280, 0, -250, 19, -26, -48,
	-462, -352, -175, 16, -60, -195, -255, -63, -121, 370,
	-198, 1, -208, -99, -66, 4, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-9, -75, 59, -120, -186, -252, -341, -388, -442, -515,
	-556, -561, -537, -570, -627, -624, -600, -622, -620, -679,
	-696, -695, -729, -748, -800, -810, -862, -718, -838, -848,
	-846, -951, -879, -1104, -813, -930, -998, -1162, -838, -935,
	-872, -949, -1108, -848, -1030, -1145, -865, -1212, -1205, -1367,
	-1233, -1205, -1226, -874, -505, -483, -449, -446, -454, -462,
	-506, -479, -490, -510, -546, -511, -592, -563, -575, -589,
	-365, -501, -409, -315, -363, -470, -386, -597, -599, -357,
	-336, -329, -365, -404, -408, -430, -432, -476, -484, -256,
	-325, -316, -268, -184, -159, -190, -207, -252, -281, -302,
	-107, -103, -57, -62, -3, 34, 56, 30, 71, 135,
	184, 138, 95, 41, 0, -43, -117, -68, -57, -59,
	-8, -21, -34, -84, 79, 169, 42, 275, 197, 54,
	228, 146, 121, 243, 176, 278, 302, 327, 325, 318,
	327, 292, 182, 25, 251, 338, 356, 384, 253, 210,
	326, 460, 448, 465, 453, 366, 606, 386, 601, 467,
	209, -101, -247, -34, 12, -191, -124, -350, 89, 128,
	67, 147, 209, 256, 184, 237, 155, 310, 361, 260,
	371, 385, 443, 462, 467, 280, 494, 344, 249, 519,
	549, 409, 319, 555, 551, 562, 591, 601, 662, 205,
	281, 334, 440, 248, 472, 510, 562, 607, 809, 463,
	807, 462, 832, 500, 250, 878, 469, -178, -442, -272,
	-439, -389, -419, -457, -402, -433, -428, -405, -441, -330,
	-352, -349, -316, -345, -588, -1019, -569, -862, -433, -374,
	-471, -577, -532, -556, -333, -225, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-19, -99, 100, -211, -273, -354, -476, -580, -676, -693,
	-752, -772, -865, -843, -889, -927, -951, -981, -981, -1044,
	-1017, -1000, -1040, -1030, -984, -995, -1067, -1014, -1094, -1076,
	-1003, -1017, -1030, -1131, -1053, -1085, -1081, -1136, -1233, -1090,
	-1155, -1169, -1099, -1067, -1117, -1276, -1058, -1504, -1856, -1482,
	-1504, -1584, -1482, -508, -415, -521, -445, -467, -471, -522,
	-510, -528, -573, -579, -579, -598, -612, -653, -636, -623,
	-482, -757, -362, -278, -314, -391, -256, -620, -669, -380,
	-359, -385, -402, -433, -438, -474, -498, -501, -540, -290,
	-312, -369, -298, -166, -152, -185, -221, -231, -292, -302,
	-108, -102, -45, -64, -24, 37, 45, 16, 68, 123,
	167, 116, 88, 52, -1, -51, -106, -25, -84, -47,
	-16, -16, -32, -83, 69, 178, 61, 310, 185, 93,
	232, 150, 82, 255, 168, 274, 308, 304, 320, 360,
	385, 336, 218, 31, 287, 429, 420, 430, 265, 212,
	321, 487, 493, 504, 506, 380, 674, 390, 702, 433,
	199, -84, -227, -62, 10, -215, -109, -439, 116, 199,
	33, 133, 203, 270, 176, 221, 123, 322, 387, 241,
	377, 429, 444, 501, 542, 297, 535, 358, 219, 598,
	573, 305, 321, 641, 620, 662, 682, 676, 722, 315,
	391, 479, 581, 245, 647, 711, 787, 785, 1199, 442,
	1206, 494, 1177, 486, 311, 1234, 483, -145, -430, -238,
	-527, -405, -373, -368, -476, -454, -377, -320, -385, -290,
	-317, -374, -270, -322, -566, -544, -653, -651, -387, -554,
	-590, -354, -491, -349, -347, -202, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-15, -126, 83, -181, -261, -335, -460, -592, -640, -716,
	-800, -774, -832, -869, -886, -965, -957, -979, -992, -1000,
	-970, -1094, -1000, -976, -989, -1062, -1076, -1140, -1030, -1113,
	-1044, -1017, -1099, -1040, -1081, -1145, -1090, -1076, -1136, -1117,
	-1090, -1198, -1099, -1184, -1169, -1140, -1076, -1525, -1504, -1616,
	-1856, -1525, -1525, -637, -419, -510, -461, -473, -565, -519,
	-569, -523, -558, -608, -608, -619, -684, -587, -631, -692,
	-350, -678, -352, -239, -344, -411, -174, -669, -647, -354,
	-370, -383, -448, -441, -431, -482, -490, -507, -534, -268,
	-342, -340, -297, -169, -173, -208, -217, -236, -283, -291,
	-109, -95, -46, -72, 18, 40, 51, 28, 79, 138,
	175, 96, 83, 44, -3, -61, -92, -19, -77, -41,
	-13, 14, -38, -64, 78, 176, 54, 311, 176, 87,
	231, 142, 106, 246, 150, 262, 311, 347, 332, 345,
	352, 346, 207, 13, 269, 393, 454, 414, 253, 165,
	291, 491, 484, 533, 498, 345, 664, 388, 685, 442,
	203, -83, -260, -60, 24, -198, -80, -348, 38, 240,
	65, 146, 218, 289, 163, 263, 100, 355, 370, 268,
	403, 441, 475, 480, 542, 305, 545, 339, 238, 598,
	593, 379, 325, 643, 631, 617, 671, 730, 712, 350,
	390, 490, 583, 228, 626, 675, 726, 832, 1192, 443,
	1277, 470, 1286, 495, 128, 1248, 426, -145, -427, -184,
	-374, -441, -363, -239, -439, -345, -426, -314, -274, -285,
	-336, -369, -335, -280, -374, -559, -694, -470, -379, -462,
	-507, -383, -379, -293, -320, -158, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,

	-1, -95, 89, -151, -218, -277, -429, -483, -610, -643,
	-756, -775, -749, -775, -839, -855, -883, -895, -921, -893,
	-908, -1017, -935, -1009, -1030, -973, -989, -976, -1014, -1090,
	-1011, -1017, -1000, -954, -1226, -1062, -1131, -1081, -1035, -1117,
	-1104, -1058, -1049, -1191, -1053, -1248, -1169, -1367, -1648, -1418,
	-1390, -1378, -1525, -844, -587, -618, -562, -573, -606, -581,
	-595, -592, -628, -670, -652, -633, -648, -721, -656, -683,
	-359, -810, -497, -505, -422, -495, -286, -694, -679, -411,
	-378, -375, -414, -464, -478, -469, -509, -488, -505, -337,
	-306, -345, -295, -169, -171, -171, -206, -211, -267, -284,
	-100, -114, -43, -58, -7, 33, 45, 22, 84, 124,
	166, 127, 99, 49, 22, -69, -97, -63, -70, -28,
	-18, -16, -49, -80, 87, 189, 83, 287, 223, 131,
	227, 150, 107, 231, 157, 261, 258, 288, 325, 358,
	380, 355, 287, 68, 322, 389, 426, 437, 308, 287,
	373, 473, 485, 535, 496, 431, 659, 484, 627, 496,
	182, -108, -288, -120, -22, -275, -200, -554, 42, 203,
	49, 154, 205, 298, 194, 246, 184, 338, 388, 287,
	427, 460, 507, 501, 591, 338, 614, 432, 278, 591,
	614, 506, 403, 702, 723, 738, 738, 741, 769, 283,
	393, 489, 593, 292, 629, 730, 744, 851, 1263, 610,
	1199, 637, 1170, 713, 246, 1177, 647, -183, -560, -333,
	-424, -538, -503, -1169, -575, -421, -479, -437, -549, -419,
	-403, -494, -368, -372, -637, -723, -1461, -850, -603, -515,
	-670, -613, -445, -484, -378, -233, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1,
}

func hashTPAQ(x, y int32) int32 {
	h := x*TPAQ_HASH1 ^ y*TPAQ_HASH2
	return h>>1 ^ h>>9 ^ x>>2 ^ y>>3 ^ TPAQ_HASH3
}

type TPAQPredictor struct {
	pr       uint  // next predicted value (0-4095)
	c0       int32 // bitwise context: last 0-7 bits with a leading 1 (1-255)
	c4       int32 // last 4 whole bytes, last is in low 8 bits
	bpos     uint  // number of bits in c0 (0-7)
	pos      int
	matchLen int
	matchPos int
	hash     int32
	apm      *TPAQAdaptiveProbMap
	mixer    *TPAQMixer
	buffer   []int8
	hashes   []int   // hash table(context, buffer position)
	states   []uint8 // hash table(context, prediction)
	cp       []int   // context pointers
	ctx      []int   // contexts
	ctxId    int
}

func NewTPAQPredictor() (*TPAQPredictor, error) {
	var err error
	this := new(TPAQPredictor)
	this.pr = 2048
	this.c0 = 1
	this.states = make([]uint8, TPAQ_MASK3+1)
	this.hashes = make([]int, TPAQ_HASH_SIZE)
	this.buffer = make([]int8, TPAQ_MASK2+1)
	this.cp = make([]int, 7)
	this.ctx = make([]int, 7)
	this.bpos = 0
	this.apm, err = newTPAQAdaptiveProbMap(65536, 7)

	if err == nil {
		this.mixer, err = newTPAQMixer(TPAQ_MIXER_SIZE)
	}

	return this, err
}

// Update the probability model
func (this *TPAQPredictor) Update(bit byte) {
	y := int(bit)
	this.mixer.update(y)
	this.bpos++
	this.c0 = (this.c0 << 1) | int32(bit)

	if this.c0 > 255 {
		this.buffer[this.pos&TPAQ_MASK2] = int8(this.c0)
		this.pos++
		this.ctxId = 0
		this.c4 = (this.c4 << 8) | (this.c0 & 0xFF)
		this.hash = (((this.hash * 43707) << 4) + int32(this.c4)) & TPAQ_MASK1
		shiftIsBinary := uint(((this.c4>>31)&1)|((this.c4>>23)&1)|
			((this.c4>>15)&1)|((this.c4>>7)&1)) << 4
		this.c0 = 1
		this.bpos = 0

		// Select Neural Net
		this.mixer.setContext(this.c4 & TPAQ_MASK0)

		// Add contexts NN
		this.addContext(int32(this.c4 ^ (this.c4 & 0xFFFF)))
		this.addContext(hashTPAQ(TPAQ_C1, this.c4<<24)) // hash with random primes
		this.addContext(hashTPAQ(TPAQ_C2, this.c4<<16))
		this.addContext(hashTPAQ(TPAQ_C3, this.c4<<8))
		this.addContext(hashTPAQ(TPAQ_C4, this.c4&-252645136))
		this.addContext(hashTPAQ(TPAQ_C5, this.c4))
		this.addContext(hashTPAQ(this.c4>>shiftIsBinary,
			(int32(this.buffer[(this.pos-8)&TPAQ_MASK2])<<16)|
				(int32(this.buffer[(this.pos-7)&TPAQ_MASK2])<<8)|
				(int32(this.buffer[(this.pos-6)&TPAQ_MASK2]))))

		// Find match
		this.findMatch()

		// Keep track of new match position
		this.hashes[this.hash] = this.pos
	}

	// Add inputs to NN
	for i := this.ctxId - 1; i >= 0; i-- {
		if this.cp[i] != 0 {
			this.states[this.cp[i]] = TPAQ_STATE_TABLE[(int(this.states[this.cp[i]])<<1)|y]
		}

		this.cp[i] = (this.ctx[i] + int(this.c0)) & TPAQ_MASK3
		this.mixer.addInput(TPAQ_STATE_MAP[(i<<8)|int(this.states[this.cp[i]])])
	}

	if this.matchLen > 0 {
		this.addMatchContext()
	}

	// Get prediction from NN
	p := this.mixer.get()

	// SSE (Secondary Symbol Estimation) 
	p = this.apm.get(y, p, int(this.c0|(this.c4&0xFF00)))
	this.pr = uint(p - ((p - 2048) >> 31))
}

// Return the split value representing the probability of 1 in the [0..4095] range.
func (this *TPAQPredictor) Get() uint {
	return this.pr
}

func (this *TPAQPredictor) findMatch() {
	// Update ongoing sequence match or detect match in the buffer (LZ like)
	if this.matchLen > 0 {
		if this.matchLen < TPAQ_MAX_LENGTH {
			this.matchLen++
		}

		this.matchPos++
	} else {
		// Retrieve match position
		this.matchPos = this.hashes[this.hash]

		// Detect match
		if this.matchPos != 0 && this.pos-this.matchPos <= TPAQ_MASK2 {
			r := this.matchLen + 1

			for r <= TPAQ_MAX_LENGTH && this.buffer[(this.pos-r)&TPAQ_MASK2] == this.buffer[(this.matchPos-r)&TPAQ_MASK2] {
				r++
			}

			this.matchLen = r - 1
		}
	}
}

func (this *TPAQPredictor) addMatchContext() {
	if this.c0 == ((int32(this.buffer[this.matchPos&TPAQ_MASK2])&0xFF)|256)>>(8-this.bpos) {
		// Add match length to NN inputs. Compute input based on run length
		var p int

		if this.matchLen < 32 {
			p = this.matchLen
		} else {
			p = (32 + ((this.matchLen - 32) >> 2))
		}

		if ((this.buffer[this.matchPos&TPAQ_MASK2] >> (7 - this.bpos)) & 1) == 0 {
			p = -p
		}

		this.mixer.addInput(p << 6)
	} else {
		this.matchLen = 0
	}
}

func (this *TPAQPredictor) addContext(cx int32) {
	cx = cx*987654323 + int32(this.ctxId)
	cx = cx<<16 | int32(uint32(cx)>>16)
	this.ctx[this.ctxId] = int(cx*123456791) + this.ctxId
	this.ctxId++
}

/////////////////////////////////////////////////////////////////
// APM maps a probability and a context into a new probability
// that bit y will next be 1.  After each guess it updates
// its state to improve future guesses.  Methods:
//
// APM a(N) creates with N contexts, uses 66*N bytes memory.
// a.p(y, pr, cx, rate=8) returned adjusted probability in context cx (0 to
//   N-1).  rate determines the learning rate (smaller = faster, default 8).
//   Probabilities are scaled 12 bits (0-4095).  Update on last bit (0-1).
//////////////////////////////////////////////////////////////////
type TPAQAdaptiveProbMap struct {
	index int   // last p, context
	rate  uint  // update rate
	data  []int // [NbCtx][33]:  p, context -> p
}

func newTPAQAdaptiveProbMap(n, rate uint) (*TPAQAdaptiveProbMap, error) {
	this := new(TPAQAdaptiveProbMap)
	this.data = make([]int, n*33)
	this.rate = rate
	k := 0

	for i := uint(0); i < n; i++ {
		for j := 0; j < 33; j++ {
			if i == 0 {
				this.data[k+j] = kanzi.Squash((j-16)<<7) << 4
			} else {
				this.data[k+j] = this.data[j]
			}
		}

		k += 33
	}

	return this, nil
}

func (this *TPAQAdaptiveProbMap) get(bit int, pr int, ctx int) int {
	g := (bit << 16) + (bit << this.rate) - (bit << 1)
	this.data[this.index] += ((g - this.data[this.index]) >> this.rate)
	this.data[this.index+1] += ((g - this.data[this.index+1]) >> this.rate)
	pr = kanzi.STRETCH[pr]
	w := pr & 127 // interpolation weight (33 points)
	this.index = ((pr + 2048) >> 7) + (ctx << 5) + ctx
	return (this.data[this.index]*(128-w) + this.data[this.index+1]*w) >> 11
}

//////////////////////////// Mixer /////////////////////////////

// Mixer combines models using 4096 neural networks with 8 inputs.
// It is used as follows:
// m.update(y) trains the network where the expected output is the last bit.
// m.addInput(stretch(p)) inputs prediction from one of N models.  The
//     prediction should be positive to predict a 1 bit, negative for 0,
//     nominally -2K to 2K.
// m.setContext(cxt) selects cxt (0..4095) as one of M neural networks to use.
// m.get() returns the (squashed) output prediction that the next bit is 1.
//  The normal sequence per prediction is:
//
// - m.addInput(x) called N times with input x=(-2047..2047)
// - m.setContext(cxt) called once with cxt=(0..M-1)
// - m.get() called once to predict the next bit, returns 0..4095
// - m.update(y) called once for actual bit y=(0..1).
type TPAQMixer struct {
	buffer []int // packed buffer: 8 inputs + 8 weights per ctx
	ctx    int   // context
	idx    int   // input index
	pr     int   // squashed prediction
}

func newTPAQMixer(size int) (*TPAQMixer, error) {
	var err error
	this := new(TPAQMixer)
	this.buffer = make([]int, size*16) // context index << 4
	this.pr = 2048
	return this, err
}

// Adjust weights to minimize coding cost of last prediction
func (this *TPAQMixer) update(bit int) {
	this.idx = 0
	err := (bit << 12) - this.pr

	if err == 0 {
		return
	}

	err = (err << 3) - err

	// Train Neural Network: update weights
	this.buffer[this.ctx+8] += ((this.buffer[this.ctx]*err + 0) >> 16)
	this.buffer[this.ctx+9] += ((this.buffer[this.ctx+1]*err + 0) >> 16)
	this.buffer[this.ctx+10] += ((this.buffer[this.ctx+2]*err + 0) >> 16)
	this.buffer[this.ctx+11] += ((this.buffer[this.ctx+3]*err + 0) >> 16)
	this.buffer[this.ctx+12] += ((this.buffer[this.ctx+4]*err + 0) >> 16)
	this.buffer[this.ctx+13] += ((this.buffer[this.ctx+5]*err + 0) >> 16)
	this.buffer[this.ctx+14] += ((this.buffer[this.ctx+6]*err + 0) >> 16)
	this.buffer[this.ctx+15] += ((this.buffer[this.ctx+7]*err + 0) >> 16)
}

func (this *TPAQMixer) setContext(ctx int32) {
	this.ctx = int(ctx << 4)
}

func (this *TPAQMixer) get() int {
	for this.idx&7 != 0 {
		this.buffer[this.ctx+this.idx] = 64
		this.idx++
	}

	// Neural Network dot product (sum weights*inputs)
	p := (this.buffer[this.ctx] * this.buffer[this.ctx+8]) +
		(this.buffer[this.ctx+1] * this.buffer[this.ctx+9]) +
		(this.buffer[this.ctx+2] * this.buffer[this.ctx+10]) +
		(this.buffer[this.ctx+3] * this.buffer[this.ctx+11]) +
		(this.buffer[this.ctx+4] * this.buffer[this.ctx+12]) +
		(this.buffer[this.ctx+5] * this.buffer[this.ctx+13]) +
		(this.buffer[this.ctx+6] * this.buffer[this.ctx+14]) +
		(this.buffer[this.ctx+7] * this.buffer[this.ctx+15])

	this.pr = kanzi.Squash(p >> 15)
	return this.pr
}

func (this *TPAQMixer) addInput(pred int) {
	this.buffer[this.ctx+this.idx] = pred
	this.idx++
}
