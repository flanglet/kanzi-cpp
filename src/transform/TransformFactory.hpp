#ifndef _TransformFactory_
#define _TransformFactory_

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include "../types.hpp"
#include "../Context.hpp"
#include "BWT.hpp"
#include "BWTS.hpp"
#include "SBRT.hpp"
#include "BWTBlockCodec.hpp"
#include "LZCodec.hpp"
#include "FSDCodec.hpp"
#include "NullTransform.hpp"
#include "ROLZCodec.hpp"
#include "RLT.hpp"
#include "SRT.hpp"
#include "TextCodec.hpp"
#include "TransformSequence.hpp"
#include "X86Codec.hpp"
#include "ZRLT.hpp"


namespace kanzi {

	template <class T>
	class TransformFactory {
	public:
		// Up to 64 transforms can be declared (6 bit index)
		static const uint64 NONE_TYPE = 0; // Copy
		static const uint64 BWT_TYPE = 1; // Burrows Wheeler
		static const uint64 BWTS_TYPE = 2; // Burrows Wheeler Scott
		static const uint64 LZ_TYPE = 3; // Lempel Ziv
		static const uint64 SNAPPY_TYPE = 4; // Snappy (obsolete)
		static const uint64 RLT_TYPE = 5; // Run Length
		static const uint64 ZRLT_TYPE = 6; // Zero Run Length
		static const uint64 MTFT_TYPE = 7; // Move To Front
		static const uint64 RANK_TYPE = 8; // Rank
		static const uint64 X86_TYPE = 9; // X86 codec
		static const uint64 DICT_TYPE = 10; // Text codec
		static const uint64 ROLZ_TYPE = 11; // ROLZ codec
		static const uint64 ROLZX_TYPE = 12; // ROLZ Extra codec
		static const uint64 SRT_TYPE = 13; // Sorted Rank
		static const uint64 LZP_TYPE = 14; // Lempel Ziv Predict
		static const uint64 FSD_TYPE = 15; // Fixed Shift Delta codec
		static const uint64 LZX_TYPE = 16; // Lempel Ziv Extra
		static const uint64 RESERVED1 = 17; // Reserved
		static const uint64 RESERVED2 = 18; // Reserved
		static const uint64 RESERVED3 = 19; // Reserved
		static const uint64 RESERVED4 = 20; // Reserved
		static const uint64 RESERVED5 = 21; // Reserved
		static const uint64 RESERVED6 = 22; // Reserved

		static uint64 getType(const char* tName) THROW;

		static uint64 getTypeToken(const char* tName) THROW;

		static std::string getName(uint64 functionType) THROW;

		static TransformSequence<T>* newTransform(Context& ctx, uint64 functionType) THROW;

	private:
		TransformFactory() {}

		~TransformFactory() {}

		static const int ONE_SHIFT = 6; // bits per transform
		static const int MAX_SHIFT = (8 - 1) * ONE_SHIFT; // 8 transforms
		static const int MASK = (1 << ONE_SHIFT) - 1;

		static Transform<T>* newToken(Context& ctx, uint64 functionType) THROW;

		static const char* getNameToken(uint64 functionType) THROW;
	};

	// The returned type contains 8 transform values
	template <class T>
	uint64 TransformFactory<T>::getType(const char* tName) THROW
	{
		std::string name(tName);
		size_t pos = name.find('+');

		if (pos == std::string::npos)
			return getTypeToken(name.c_str()) << MAX_SHIFT;

		size_t prv = 0;
		int n = 0;
		uint64 res = 0;
		int shift = MAX_SHIFT;
		name += '+';

		while (pos != std::string::npos) {
			n++;

			if (n > 8) {
				std::stringstream ss;
				ss << "Only 8 transforms allowed: " << name;
				throw std::invalid_argument(ss.str());
			}

			std::string token = name.substr(prv, pos - prv);
			uint64 typeTk = getTypeToken(token.c_str());

			// Skip null transform
			if (typeTk != NONE_TYPE) {
				res |= (typeTk << shift);
				shift -= ONE_SHIFT;
			}

			prv = pos + 1;
			pos = name.find('+', prv);
		}

		return res;
	}

	template <class T>
	uint64 TransformFactory<T>::getTypeToken(const char* tName) THROW
	{
		std::string name(tName);
		transform(name.begin(), name.end(), name.begin(), ::toupper);

		if (name == "TEXT")
			return DICT_TYPE;

		if (name == "BWT")
			return BWT_TYPE;

		if (name == "BWTS")
			return BWTS_TYPE;

		if (name == "ROLZ")
			return ROLZ_TYPE;

		if (name == "ROLZX")
			return ROLZX_TYPE;

		if (name == "MTFT")
			return MTFT_TYPE;

		if (name == "ZRLT")
			return ZRLT_TYPE;

		if (name == "RLT")
			return RLT_TYPE;

		if (name == "SRT")
			return SRT_TYPE;

		if (name == "RANK")
			return RANK_TYPE;

		if (name == "LZ")
			return LZ_TYPE;

		if (name == "LZX")
			return LZX_TYPE;

		if (name == "LZP")
			return LZP_TYPE;

		if (name == "X86")
			return X86_TYPE;

		if (name == "FSD")
			return FSD_TYPE;

		if (name == "NONE")
			return NONE_TYPE;

		std::stringstream ss;
		ss << "Unknown transform type: '" << name << "'";
		throw std::invalid_argument(ss.str());
	}

	template <class T>
	TransformSequence<T>* TransformFactory<T>::newTransform(Context& ctx, uint64 functionType) THROW
	{
		Transform<T>* transforms[8];
		int nbtr = 0;

		for (int i = 0; i < 8; i++) {
			transforms[i] = nullptr;
			const uint64 t = (functionType >> (MAX_SHIFT - ONE_SHIFT * i)) & MASK;

			if ((t != NONE_TYPE) || (i == 0))
				transforms[nbtr++] = newToken(ctx, t);
		}

		return new TransformSequence<T>(transforms, true);
	}

	template <class T>
	Transform<T>* TransformFactory<T>::newToken(Context& ctx, uint64 functionType) THROW
	{
		switch (functionType) {
		case DICT_TYPE: {
			int textCodecType = 1;

			if (ctx.has("codec")) {			
				std::string entropyType = ctx.getString("codec");
				transform(entropyType.begin(), entropyType.end(), entropyType.begin(), ::toupper);

				// Select text encoding based on entropy codec.
				if ((entropyType == "NONE") || (entropyType == "ANS0") ||
				   (entropyType == "HUFFMAN") || (entropyType == "RANGE"))
				    textCodecType = 2;
			}

			ctx.putInt("textcodec", textCodecType);
			return new TextCodec(ctx);
		}

		case ROLZ_TYPE:
			return new ROLZCodec(ctx);

		case ROLZX_TYPE:
			return new ROLZCodec(ctx);

		case BWT_TYPE:
			return new BWTBlockCodec(ctx);

		case BWTS_TYPE:
			return new BWTS(ctx);

		case LZX_TYPE:
			ctx.putInt("lz", LZX_TYPE);
			return new LZCodec(ctx);

		case LZ_TYPE:
			ctx.putInt("lz", LZ_TYPE);
			return new LZCodec(ctx);

		case LZP_TYPE:
			ctx.putInt("lz", LZP_TYPE);
			return new LZCodec(ctx);

		case RANK_TYPE:
			return new SBRT(SBRT::MODE_RANK, ctx);

		case SRT_TYPE:
			return new SRT(ctx);

		case MTFT_TYPE:
			return new SBRT(SBRT::MODE_MTF, ctx);

		case ZRLT_TYPE:
			return new ZRLT(ctx);

		case RLT_TYPE:
			return new RLT(ctx);

		case X86_TYPE:
			return new X86Codec(ctx);

		case FSD_TYPE:
			return new FSDCodec(ctx);

		case NONE_TYPE:
			return new NullTransform<T>(ctx);

		default:
			std::stringstream ss;
			ss << "Unknown transform type: '" << functionType << "'";
			throw std::invalid_argument(ss.str());
		}
	}

	template <class T>
	std::string TransformFactory<T>::getName(uint64 functionType) THROW
	{
		std::string res;

		for (int i = 0; i < 8; i++) {
			const uint64 t = (functionType >> (MAX_SHIFT - ONE_SHIFT * i)) & MASK;

			if (t == NONE_TYPE)
				continue;

			if (res.length() > 0)
				res += '+';

			res += getNameToken(t);
		}

		return (res.length() == 0) ? getNameToken(NONE_TYPE) : res;
	}

	template <class T>
	const char* TransformFactory<T>::getNameToken(uint64 functionType) THROW
	{
		switch (functionType) {
		case DICT_TYPE:
			return "TEXT";

		case BWT_TYPE:
			return "BWT";

		case BWTS_TYPE:
			return "BWTS";

		case ROLZ_TYPE:
			return "ROLZ";

		case ROLZX_TYPE:
			return "ROLZX";

		case LZ_TYPE:
			return "LZ";

		case LZX_TYPE:
			return "LZX";

		case LZP_TYPE:
			return "LZP";

		case ZRLT_TYPE:
			return "ZRLT";

		case RLT_TYPE:
			return "RLT";

		case SRT_TYPE:
			return "SRT";

		case RANK_TYPE:
			return "RANK";

		case MTFT_TYPE:
			return "MTFT";

		case X86_TYPE:
			return "X86";

		case FSD_TYPE:
			return "FSD";

		case NONE_TYPE:
			return "NONE";

		default:
			std::stringstream ss;
			ss << "Unknown transform type: '" << functionType << "'";
			throw std::invalid_argument(ss.str());
		}
	}
}

#endif
