/*
Copyright 2011-2017 Frederic Langlet
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

#ifndef _TextCodec_
#define _TextCodec_

#include <map>
#include "../Function.hpp"

using namespace std;

namespace kanzi {
   class DictEntry {
   public:
       int32 _hash; // full word hash
       int32 _data; // packed word length (8 MSB) + index in dictionary (24 LSB)
       const byte* _ptr; // text data

       DictEntry();

       DictEntry(const byte* ptr, int32 hash, int idx, int length);

       DictEntry(const DictEntry& de);

       DictEntry& operator = (const DictEntry& de);

       ~DictEntry() {}
   };


   class TextCodec1 : public Function<byte> {
   public:
       TextCodec1();

       TextCodec1(map<string, string>& ctx);

       virtual ~TextCodec1()
       {
           delete[] _dictList;
           delete[] _dictMap;
       }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length);

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length);

       // Limit to 1 x srcLength and let the caller deal with
       // a failure when the output is not smaller than the input
       inline int getMaxEncodedLength(int srcLen) const { return srcLen; }

   private:
       DictEntry** _dictMap;
       DictEntry* _dictList;
       int _freqs[257][256];
       byte _escapes[2];
       int _staticDictSize;
       int _dictSize;
       int _logHashSize;
       int32 _hashMask;
       bool _isCRLF; // EOL = CR + LF

       bool expandDictionary();
       int emitWordIndex(byte dst[], int val);
       int emitSymbols(byte src[], byte dst[], const int srcEnd, const int dstEnd);
   };


   class TextCodec2 : public Function<byte> {
   public:
       TextCodec2();

       TextCodec2(map<string, string>& ctx);

       virtual ~TextCodec2()
       {
           delete[] _dictList;
           delete[] _dictMap;
       }

       bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length);

       bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length);

       // Limit to 1 x srcLength and let the caller deal with
       // a failure when the output is not smaller than the input
       inline int getMaxEncodedLength(int srcLen) const { return srcLen; }

   private:
       DictEntry** _dictMap;
       DictEntry* _dictList;
       int _freqs[257][256];
       byte _escapes[2];
       int _staticDictSize;
       int _dictSize;
       int _logHashSize;
       int32 _hashMask;
       bool _isCRLF; // EOL = CR + LF

       bool expandDictionary();
       int emitWordIndex(byte dst[], int val, int mask);
       int emitSymbols(byte src[], byte dst[], const int srcEnd, const int dstEnd);
   };


   // Simple one-pass text codec. Uses a default (small) static dictionary
   // or potentially larger custom one. Generates a dynamic dictionary.
   class TextCodec : public Function<byte> {
      friend class TextCodec1;
      friend class TextCodec2;

   public:
       static const int THRESHOLD1 = 128;
       static const int THRESHOLD2 = THRESHOLD1 * THRESHOLD1;
       static const int THRESHOLD3 = 32;
       static const int THRESHOLD4 = THRESHOLD3 * 128;
       static const int MAX_DICT_SIZE = 1 << 19; // must be less than 1<<24
       static const int MAX_WORD_LENGTH = 32; // must be less than 128
       static const int LOG_HASHES_SIZE = 24; // 16 MB
       static const byte ESCAPE_TOKEN1 = byte(0x0F); // dictionary word preceded by space symbol
       static const byte ESCAPE_TOKEN2 = byte(0x0E); // toggle upper/lower case of first word char

       TextCodec();

       TextCodec(map<string, string>& ctx);

       virtual ~TextCodec()
       {
           delete _delegate;
       }

       inline bool forward(SliceArray<byte>& src, SliceArray<byte>& dst, int length)
       {
          return _delegate->forward(src, dst, length);
       }

       inline bool inverse(SliceArray<byte>& src, SliceArray<byte>& dst, int length)
       {
          return _delegate->inverse(src, dst, length);
       }

       inline int getMaxEncodedLength(int srcLen) const 
       { 
          return _delegate->getMaxEncodedLength(srcLen) ; 
       }

       inline static bool isText(byte val) { return isLowerCase(val) || isUpperCase(val); }

       inline static bool isLowerCase(byte val) { return (val >= 'a') && (val <= 'z'); }

       inline static bool isUpperCase(byte val) { return (val >= 'A') && (val <= 'Z'); }

       inline static bool isDelimiter(byte val) { return DELIMITER_CHARS[val & 0xFF]; }

   private:
       static const int32 HASH1 = 0x7FEB352D;
       static const int32 HASH2 = 0x846CA68B;
       static const byte CR = byte(0x0D);
       static const byte LF = byte(0x0A);

       static bool* initDelimiterChars();
       static const bool* DELIMITER_CHARS;

       static SliceArray<byte> unpackDictionary32(const byte dict[], int dictSize);

       static bool sameWords(const byte src[], byte dst[], const int length);

       static byte computeStats(byte block[], int count, int freqs[][256]);

       // Default dictionary
       static const byte DICT_EN_1024[];

       // Static dictionary of 1024 entries.
       static DictEntry STATIC_DICTIONARY[1024];
       static int createDictionary(SliceArray<byte> words, DictEntry dict[], int maxWords, int startWord);
       static const int STATIC_DICT_WORDS;

       Function<byte>* _delegate;
   };


   inline DictEntry::DictEntry()
   {
       _ptr = nullptr;
       _hash = 0;
       _data = 0;
   }

   inline DictEntry::DictEntry(const byte* ptr, int32 hash, int idx, int length)
   {
       _ptr = ptr;
       _hash = hash;
       _data = ((length & 0xFF) << 24) | (idx & 0x00FFFFFF);
   }

   inline DictEntry::DictEntry(const DictEntry& de)
   {
       _ptr = de._ptr;
       _hash = de._hash;
       _data = de._data;
   }

   inline DictEntry& DictEntry::operator = (const DictEntry& de)
   {
       _ptr = de._ptr;
       _hash = de._hash;
       _data = de._data;
       return *this;
   }
}
#endif
