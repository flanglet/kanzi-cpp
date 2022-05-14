#include "libapi.hpp"
#include "../types.hpp"
#include "../Error.hpp"
#include "../io/CompressedInputStream.hpp"
#include "../io/CompressedOutputStream.hpp"
#include "../transform/TransformFactory.hpp"
#include "../entropy/EntropyCodecFactory.hpp"

#include <errno.h>

#ifdef _MSC_VER
   #include <io.h>
   #define FILENO _fileno
   #define READ _read
   #define WRITE _write
#else
   #include <unistd.h>
   #define FILENO fileno
   #define READ read
   #define WRITE write
#endif

using namespace std;
using namespace kanzi;

namespace kanzi {
   // Utility classes to map C FILEs to C++ streams
   class ofstreambuf : public streambuf
   {
     public:
        ofstreambuf (int fd) : _fd(fd) { }

     private:
        int _fd; 

        virtual int_type overflow(int_type c) {
            if (c == EOF)
               return EOF;

            char d = c;
            return (WRITE(_fd, &d, 1) == 1) ? c : EOF;
        }

        virtual streamsize xsputn(const char* s, streamsize sz) {
            return WRITE(_fd, s, sz);
        }
    };


   class ifstreambuf : public streambuf {
     public:
       ifstreambuf(int fd) : _fd(fd) {
           setg(&_buffer[4], &_buffer[4], &_buffer[4]);
       }

     private:
       static const int BUF_SIZE = 1024 + 4;
       int _fd;
       char _buffer[BUF_SIZE];

       virtual int_type underflow() {
           if (gptr() < egptr())
               return traits_type::to_int_type(*gptr());

           const int pbSz = min(int(gptr() - eback()), BUF_SIZE - 4);
           memmove(&_buffer[BUF_SIZE - pbSz], gptr() - pbSz, pbSz);
           const int r = READ(_fd, &_buffer[4], BUF_SIZE - 4);

           if (r <= 0)
               return EOF;

           setg(&_buffer[4 - pbSz], &_buffer[4], &_buffer[4 + r]);
           return traits_type::to_int_type(*gptr());
       }
    };


    class FileOutputStream : public ostream
    {
       private:
          ofstreambuf _buf;

       public:
          FileOutputStream(int fd) : ostream(0), _buf(fd) {
              rdbuf(&_buf);
          }
    };


    class FileInputStream : public istream
    {
       private:
          ifstreambuf _buf;

       public:
          FileInputStream(int fd) : istream(0), _buf(fd) {
             rdbuf(&_buf);
          }
    };
}


// Create internal cContext and CompressedOutputStream
int CDECL initCompressor(struct cData* pData, FILE* dst, struct cContext** pCtx)
{
    if ((pData == nullptr) || (pCtx == nullptr) || (dst == nullptr))
        return Error::ERR_INVALID_PARAM;

    try {
        int mLen;

        // Process params
        string transform = TransformFactory<byte>::getName(TransformFactory<byte>::getType(pData->transform));
        mLen = min(int(transform.length()), 63);
        strncpy(pData->transform, transform.data(), mLen);
        pData->transform[mLen + 1] = 0;
        string entropy = EntropyCodecFactory::getName(EntropyCodecFactory::getType(pData->entropy));
        mLen = min(int(entropy.length()), 63);
        strncpy(pData->entropy, entropy.data(), mLen);
        pData->entropy[mLen + 1] = 0;
        pData->blockSize = (pData->blockSize + 15) & -16;
        cContext* cctx = new cContext();
        const int fd = FILENO(dst);

        if (fd == -1)
           return Error::ERR_CREATE_COMPRESSOR;

        // Create compression stream and update context
        FileOutputStream* fos = new FileOutputStream(fd);
        cctx->pCos = new CompressedOutputStream(*fos, pData->entropy, pData->transform, pData->blockSize, bool(pData->checksum & 1), pData->jobs);
        cctx->blockSize = pData->blockSize;
        cctx->fos = fos;
        *pCtx = cctx;
    }
    catch (exception&) {
        return Error::ERR_CREATE_COMPRESSOR;
    }

    return 0;
}

int CDECL compress(struct cContext* pCtx, const BYTE* src, int* inSize, int* outSize)
{
    *outSize = 0;
    int res = 0;

    if ((pCtx == nullptr) || (*inSize > int(pCtx->blockSize))) {
        *inSize = 0;
        return Error::ERR_INVALID_PARAM;
    }

    CompressedOutputStream* pCos = (CompressedOutputStream*)pCtx->pCos;

    if (pCos == nullptr)
        return Error::ERR_INVALID_PARAM;

    try {
        const uint64 w = pCos->getWritten();
        pCos->write((const char*)src, streamsize(*inSize));
        res = pCos->good() ? 0 : Error::ERR_WRITE_FILE;
        *outSize = int(pCos->getWritten() - w);
    }
    catch (exception&) {
        *inSize = 0; // revisit: could be more accurate
        return Error::ERR_UNKNOWN;
    }

    return res;
}

// Cleanup allocated internal data structures
int CDECL disposeCompressor(struct cContext* pCtx, int* outSize)
{
	 *outSize = 0;

    if (pCtx == nullptr)
        return Error::ERR_INVALID_PARAM;

    CompressedOutputStream* pCos = (CompressedOutputStream*)pCtx->pCos;

    try {
        if (pCos != nullptr) {
            const uint64 w = pCos->getWritten();
            pCos->close();
            *outSize = int(pCos->getWritten() - w);
		  }
    }
    catch (exception&) {
        return Error::ERR_UNKNOWN;
    }

    try {
        if (pCos != nullptr)
            delete pCos;

        pCos = nullptr;

        if (pCtx != nullptr) {
           if (pCtx->fos != nullptr)
               delete (FileOutputStream*)pCtx->fos;

           pCtx->fos = nullptr;
           delete pCtx;
           pCtx = nullptr;
        }
    }
    catch (exception&) {
        pCos = nullptr;

        if (pCtx != nullptr) {
           if (pCtx->fos != nullptr)
               delete (FileOutputStream*)pCtx->fos;

           pCtx->fos = nullptr;
           delete pCtx;
           pCtx = nullptr;
        }

        return Error::ERR_UNKNOWN;
    }

    return 0;
}

// Create internal dContext and CompressedInputStream
int CDECL initDecompressor(struct dData* pData, FILE* src, struct dContext** pCtx)
{
    if ((pData == nullptr) || (pCtx == nullptr) || (src == nullptr))
        return Error::ERR_INVALID_PARAM;

    if (pData->bufferSize > uint(2) * 1024 * 1024 * 1024) // max buffer size
        return Error::ERR_INVALID_PARAM;

    try {
        const int fd = FILENO(src);

        if (fd == -1)
           return Error::ERR_CREATE_COMPRESSOR;

        // Create decompression stream and update context
        FileInputStream* fis = new FileInputStream(fd);
        dContext* dctx = new dContext();
        dctx->pCis = new CompressedInputStream(*fis, pData->jobs);
        dctx->bufferSize = pData->bufferSize;
        dctx->fis = fis;
        *pCtx = dctx;
    }
    catch (exception&) {
        return Error::ERR_CREATE_DECOMPRESSOR;
    }

    return 0;
}

int CDECL decompress(struct dContext* pCtx, BYTE* dst, int* inSize, int* outSize)
{
    *inSize = 0;
    int res = 0;

    if ((pCtx == nullptr) || (*outSize > int(pCtx->bufferSize))) {
        *outSize = 0;
        return Error::ERR_INVALID_PARAM;
    }

    CompressedInputStream* pCis = (CompressedInputStream*)pCtx->pCis;

    if (pCis == nullptr)
        return Error::ERR_INVALID_PARAM;

    try {
        const uint64 r = int(pCis->getRead());
        pCis->read((char*)dst, streamsize(*outSize));
        res = pCis->good() ? 0 : Error::ERR_READ_FILE;
        *inSize = int(pCis->getRead() - r);
        *outSize = int(pCis->gcount());
    }
    catch (exception&) {
        *outSize = 0; // revisit: could be more accurate
        return Error::ERR_UNKNOWN;
    }

    return res;
}

// Cleanup allocated internal data structures
int CDECL disposeDecompressor(struct dContext* pCtx)
{
    if (pCtx == nullptr)
        return Error::ERR_INVALID_PARAM;

    CompressedInputStream* pCis = (CompressedInputStream*)pCtx->pCis;

    try {
        if (pCis != nullptr) {
            pCis->close();
        }
    }
    catch (exception&) {
        return Error::ERR_UNKNOWN;
    }

    try {
        if (pCis != nullptr)
            delete pCis;

        if (pCtx != nullptr) {
           if (pCtx->fis != nullptr)
               delete (FileInputStream*)pCtx->fis;

           pCtx->fis = nullptr;
           delete pCtx;
           pCtx = nullptr;
        }
    }
    catch (exception&) {
        pCis = nullptr;

        if (pCtx != nullptr) {
           if (pCtx->fis != nullptr)
               delete (FileInputStream*)pCtx->fis;

           pCtx->fis = nullptr;
           delete pCtx;
           pCtx = nullptr;
        }

        pCtx = nullptr;
        return Error::ERR_UNKNOWN;
    }

    pCis = nullptr;
    pCtx = nullptr;
    return 0;
}
