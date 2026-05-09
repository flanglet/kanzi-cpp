# Kanzi C++ API Reference

This document describes the public integration APIs exposed by Kanzi C++:

- The C ABI in `src/api/Compressor.hpp` and `src/api/Decompressor.hpp`
- The C++ stream API in `src/io/CompressedOutputStream.hpp` and `src/io/CompressedInputStream.hpp`
- Shared C++ extension interfaces for transforms, entropy codecs, bitstreams, contexts, events, and errors
- The Python `ctypes` wrapper in `src/api/kanzi.py`

Kanzi is a compressor, not an archive format. The APIs below operate on byte streams or blocks; callers remain responsible for file naming, metadata, multi-file packaging, and application-level lifecycle decisions.

## Contents

- [Compatibility](#compatibility)
- [Common Concepts](#common-concepts)
- [C API](#c-api)
- [C++ Stream API](#c-stream-api)
- [Context Keys](#context-keys)
- [Transforms](#transforms)
- [Entropy Codecs](#entropy-codecs)
- [Bitstream API](#bitstream-api)
- [Events and Listeners](#events-and-listeners)
- [Errors and Exceptions](#errors-and-exceptions)
- [Python Wrapper](#python-wrapper)
- [Integration Notes](#integration-notes)

## Compatibility

The core library is written in portable C++ and supports multiple C++ standards. Some APIs expose extra functionality when the compiler supports it:

- C++98 and later: core C++ stream and codec APIs
- C++11 and later: move constructors/assignments in several value types
- C++17 and later: `Context` stores values internally with `std::variant`
- `CONCURRENCY_ENABLED`: multi-job compression/decompression and `ThreadPool` integration
- MSVC 2008: some seek helpers are unavailable because the bitstream classes do not inherit `Seekable` on that compiler path

The C API is exported with `extern "C"` and uses opaque contexts plus `FILE*`.

## Common Concepts

### Namespace and Types

Most C++ APIs live in namespace `kanzi`.

Common integer aliases are defined in `src/types.hpp`:

| Alias | Meaning |
| --- | --- |
| `byte` | Byte type. `std::byte` when enabled with C++17 and `KNZ_BYTE_AS_STD_BYTE`, otherwise `uint8_t`. |
| `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32` | Fixed-width integer aliases. |
| `uint` | `uint32_t`. |
| `int64`, `uint64` | 64-bit signed and unsigned aliases. |

### Transform and Entropy Names

Names are case-insensitive when parsed through factories or the C API.

Supported transform names:

| Name | Description |
| --- | --- |
| `NONE` | No transform. |
| `PACK` | Alias packing transform. |
| `DNA` | DNA-specialized alias packing. |
| `BWT` | Burrows-Wheeler transform block codec. |
| `BWTS` | Burrows-Wheeler Scott transform. |
| `LZ` | Default Lempel-Ziv transform. |
| `LZX` | Lempel-Ziv extra variant. |
| `LZP` | Lempel-Ziv predictive transform. |
| `ROLZ` | Reduced-offset Lempel-Ziv transform. |
| `ROLZX` | Extra ROLZ variant. |
| `RLT` | Run-length transform. |
| `ZRLT` | Zero run-length transform. |
| `MTFT` | Move-to-front family transform. |
| `RANK` | Rank transform. |
| `SRT` | Sorted-rank transform. |
| `TEXT` | Text/dictionary transform. |
| `MM` | Multimedia/FSD transform. |
| `EXE` | Executable-code transform. |
| `UTF` | UTF transform. |

Transforms can be chained with `+`, for example `BWT+RANK+ZRLT`. Up to 8 transform tokens are accepted.

Supported entropy names:

| Name | Description |
| --- | --- |
| `NONE` | No entropy coding. |
| `HUFFMAN` | Huffman coding. |
| `ANS0` | ANS range coding, order 0. |
| `ANS1` | ANS range coding, order 1. |
| `RANGE` | Range coding. |
| `FPAQ` | Fast PAQ-style bit coding. |
| `CM` | Context model. |
| `TPAQ` | Tangelo PAQ. |
| `TPAQX` | Tangelo PAQ extra. |

### Blocks

Kanzi compresses data in blocks.

General stream constraints:

| Parameter | Constraint |
| --- | --- |
| Block size | `1024` to `1024 * 1024 * 1024` bytes |
| Block size alignment | Multiple of 16 for compression streams |
| Checksum size | `0`, `32`, or `64` |
| Jobs | `1..64` when concurrency is enabled, exactly `1` otherwise |

The C API rounds `blockSize` up to the next multiple of 16 during initialization.

### Headered vs Headerless Streams

Default streams are headered. The compressed stream contains the transform, entropy codec, checksum size, block size, and bitstream version.

Headerless mode omits this metadata. Callers must provide matching parameters to both the compressor and decompressor.

Use headerless mode only when an outer protocol already carries the Kanzi stream parameters.

## C API

Headers:

```c
#include "api/Compressor.hpp"
#include "api/Decompressor.hpp"
```

The C API returns `0` on success and a Kanzi error code on failure. Error codes are listed in [Errors and Exceptions](#errors-and-exceptions).

### C Compression Types

```c
struct cContext;

struct cData {
    char transform[64];
    char entropy[16];
    size_t blockSize;
    unsigned int jobs;
    int checksum;
    int headerless;
};
```

`cContext` is opaque. Do not allocate or inspect it directly.

`cData` fields:

| Field | Direction | Description |
| --- | --- | --- |
| `transform` | in/out | Transform name or chain. Validated and rewritten to canonical uppercase form by `initCompressor()`. |
| `entropy` | in/out | Entropy codec name. Validated and rewritten to canonical uppercase form by `initCompressor()`. |
| `blockSize` | in/out | Maximum input bytes accepted per `compress()` call. Rounded up to a multiple of 16 by `initCompressor()`. |
| `jobs` | in | Maximum concurrent jobs. |
| `checksum` | in | `0`, `32`, or `64`. |
| `headerless` | in | Non-zero writes a headerless bitstream. |

### `getCompressorVersion`

```c
unsigned int getCompressorVersion(void);
```

Returns the compressor C API version packed as:

```text
(major << 16) | (minor << 8) | patch
```

Current header constants are `KANZI_COMP_VERSION_MAJOR`, `KANZI_COMP_VERSION_MINOR`, and `KANZI_COMP_VERSION_PATCH`.

### `initCompressor`

```c
int initCompressor(struct cData* cParam, FILE* dst, struct cContext** ctx);
```

Initializes a compressor writing to `dst`.

Rules:

- `cParam`, `dst`, and `ctx` must be non-null.
- `transform` and `entropy` must be null-terminated inside their fixed arrays.
- `transform` and `entropy` are validated and normalized in place.
- `blockSize` is rounded up to a multiple of 16.
- On success, `*ctx` receives a new compressor context.
- On failure, no compressor context should be used.

Common return codes:

| Code | Meaning |
| --- | --- |
| `0` | Success. |
| `ERR_INVALID_PARAM` | Null argument, unterminated string, invalid parameter. |
| `ERR_CREATE_COMPRESSOR` | Could not create the internal compressor stream. |

### `compress`

```c
int compress(struct cContext* ctx,
             const unsigned char* src,
             size_t inSize,
             size_t* outSize);
```

Compresses one input block and writes compressed bytes to the destination `FILE*` provided at initialization.

Rules:

- `ctx` and `outSize` must be non-null.
- `src` may be null only when `inSize == 0`.
- `inSize` must not exceed the initialized `blockSize`.
- `*outSize` is set to the number of bytes written during this call.
- Zero-length input is accepted and reports zero output.

The compressor owns no input memory. The caller may reuse or free `src` after the call returns.

### `disposeCompressor`

```c
int disposeCompressor(struct cContext** ctx, size_t* outSize);
```

Closes the compressor, flushes remaining internal data, releases resources, and sets `*ctx` to null.

Rules:

- `ctx`, `*ctx`, and `outSize` must be non-null.
- `*outSize` receives bytes written during final flush/close.
- Always call this after successful `initCompressor()`.

### C Decompression Types

```c
struct dContext;

struct dData {
    size_t bufferSize;
    unsigned int jobs;
    int headerless;

    char transform[64];
    char entropy[16];
    unsigned int blockSize;
    size_t originalSize;
    int checksum;
    int bsVersion;
};
```

`dContext` is opaque.

`dData` fields:

| Field | Direction | Required | Description |
| --- | --- | --- | --- |
| `bufferSize` | in | Always | Maximum output bytes accepted in one `decompress()` call. Must not exceed 2 GiB. |
| `jobs` | in | Always | Maximum concurrent jobs. |
| `headerless` | in | Always | Non-zero means the compressed stream has no header. |
| `transform` | in/out | Headerless | Transform name or chain. Validated and normalized in headerless mode. |
| `entropy` | in/out | Headerless | Entropy codec name. Validated and normalized in headerless mode. |
| `blockSize` | in/out | Headerless | Original compression block size. Rounded up to a multiple of 16 by `initDecompressor()`. |
| `originalSize` | in | Headerless | Original stream size, when known. |
| `checksum` | in | Headerless | `0`, `32`, or `64`. Must match compression. |
| `bsVersion` | in | Headerless | Bitstream version to decode. |

For headered streams, only `bufferSize`, `jobs`, and `headerless = 0` are required. Stream metadata is read from the bitstream.

### `getDecompressorVersion`

```c
unsigned int getDecompressorVersion(void);
```

Returns the decompressor C API version packed as:

```text
(major << 16) | (minor << 8) | patch
```

Current header constants are `KANZI_DECOMP_VERSION_MAJOR`, `KANZI_DECOMP_VERSION_MINOR`, and `KANZI_DECOMP_VERSION_PATCH`.

### `initDecompressor`

```c
int initDecompressor(struct dData* dParam, FILE* src, struct dContext** ctx);
```

Initializes a decompressor reading from `src`.

Rules:

- `dParam`, `src`, and `ctx` must be non-null.
- `bufferSize` must be at most 2 GiB.
- In headerless mode, `transform` and `entropy` must be null-terminated inside their fixed arrays.
- In headerless mode, `transform` and `entropy` are validated and normalized in place.
- On success, `*ctx` receives a new decompressor context.

### `decompress`

```c
int decompress(struct dContext* ctx,
               unsigned char* dst,
               size_t* inSize,
               size_t* outSize);
```

Decompresses bytes into `dst`.

Rules:

- `ctx` and `outSize` must be non-null.
- `*outSize` is the destination capacity on input.
- `*outSize` must be less than or equal to the initialized `bufferSize`.
- If input `*outSize == 0`, the call returns success and writes nothing.
- `dst` must be non-null when input `*outSize != 0`.
- On success, `*outSize` is replaced with the number of bytes decoded.
- If `inSize` is non-null, it receives the number of compressed bytes read during the call.

End of stream is reported as a successful call with `*outSize == 0`.

### `disposeDecompressor`

```c
int disposeDecompressor(struct dContext** ctx);
```

Closes the decompressor, releases resources, and sets `*ctx` to null.

Rules:

- `ctx` and `*ctx` must be non-null.
- Always call this after successful `initDecompressor()`.

### C Example

```c
#include <stdio.h>
#include <string.h>
#include "api/Compressor.hpp"
#include "api/Decompressor.hpp"

int write_kanzi_file(const char* path, const unsigned char* data, size_t len) {
    FILE* out = fopen(path, "wb");
    if (out == NULL)
        return -1;

    struct cData params;
    memset(&params, 0, sizeof(params));
    strcpy(params.transform, "LZ");
    strcpy(params.entropy, "ANS0");
    params.blockSize = 1 << 20;
    params.jobs = 1;
    params.checksum = 32;
    params.headerless = 0;

    struct cContext* ctx = NULL;
    int rc = initCompressor(&params, out, &ctx);
    if (rc != 0) {
        fclose(out);
        return rc;
    }

    size_t written = 0;
    rc = compress(ctx, data, len, &written);

    size_t flushed = 0;
    int close_rc = disposeCompressor(&ctx, &flushed);
    fclose(out);

    return (rc != 0) ? rc : close_rc;
}
```

## C++ Stream API

Headers:

```cpp
#include "io/CompressedOutputStream.hpp"
#include "io/CompressedInputStream.hpp"
```

`kanzi::InputStream` is an alias for `std::istream`.
`kanzi::OutputStream` is an alias for `std::ostream`.

The compressed streams derive from those standard stream types by using the wrapped stream's `rdbuf()`. The caller owns the underlying stream and must keep it alive for the lifetime of the Kanzi stream.

### `CompressedOutputStream`

```cpp
class kanzi::CompressedOutputStream : public kanzi::OutputStream {
public:
    CompressedOutputStream(OutputStream& os,
                           int jobs = 1,
                           const std::string& entropy = "NONE",
                           const std::string& transform = "NONE",
                           int blockSize = 4 * 1024 * 1024,
                           int checksum = 0,
                           uint64 originalSize = 0,
#ifdef CONCURRENCY_ENABLED
                           ThreadPool* pool = nullptr,
#endif
                           bool headerless = false);

    CompressedOutputStream(OutputStream& os, Context& ctx, bool headerless = false);

    ~CompressedOutputStream();

    bool addListener(Listener<Event>& listener);
    bool removeListener(Listener<Event>& listener);

    std::ostream& write(const char* data, std::streamsize length);
    std::ostream& put(char c);
    std::ostream& flush();
    std::streampos tellp();
    std::ostream& seekp(std::streampos pos);
    void close();
    uint64 getWritten() const;
};
```

Constructor parameters:

| Parameter | Description |
| --- | --- |
| `os` | Destination stream. Must remain alive. |
| `jobs` | Number of concurrent jobs. |
| `entropy` | Entropy codec name. |
| `transform` | Transform name or chain. |
| `blockSize` | Compression block size in bytes. Must be `1024..1 GiB` and a multiple of 16. |
| `checksum` | `0`, `32`, or `64`. |
| `originalSize` | Optional original input size stored in the header when known. |
| `pool` | Optional caller-owned thread pool when concurrency is enabled. |
| `headerless` | If true, omit stream header. |

Context constructor keys are listed in [Context Keys](#context-keys).

Methods:

| Method | Description |
| --- | --- |
| `write(data, length)` | Compresses `length` bytes from `data`. Throws on negative length, closed stream, write failure, or codec failure. |
| `put(c)` | Writes one byte. |
| `flush()` | No-op for Kanzi buffering. Underlying stream flushing remains caller-controlled. |
| `close()` | Finishes all pending blocks, closes the compressed bitstream, and releases internal resources. |
| `getWritten()` | Returns compressed bytes written so far. |
| `addListener(listener)` | Registers an event listener. |
| `removeListener(listener)` | Unregisters an event listener. Returns false if not registered. |
| `tellp()` | Not supported. Throws `std::ios_base::failure`. |
| `seekp(pos)` | Not supported. Throws `std::ios_base::failure`. |

Call `close()` before destroying or reading the generated compressed data. The destructor also cleans up, but explicit close makes error handling deterministic.

### `CompressedInputStream`

```cpp
class kanzi::CompressedInputStream : public kanzi::InputStream {
public:
    CompressedInputStream(InputStream& is,
                          int jobs = 1,
                          const std::string& entropy = "NONE",
                          const std::string& transform = "NONE",
                          int blockSize = 4 * 1024 * 1024,
                          int checksum = 0,
                          uint64 originalSize = 0,
#ifdef CONCURRENCY_ENABLED
                          ThreadPool* pool = nullptr,
#endif
                          bool headerless = false,
                          int bsVersion = BITSTREAM_FORMAT_VERSION);

    CompressedInputStream(InputStream& is, Context& ctx, bool headerless = false);

    ~CompressedInputStream();

    bool addListener(Listener<Event>& listener);
    bool removeListener(Listener<Event>& listener);

    std::istream& read(char* data, std::streamsize length);
    std::streamsize gcount() const;
    int get();
    int peek();
    void close();
    uint64 getRead() const;

#if !defined(_MSC_VER) || _MSC_VER > 1500
    bool seek(int64 bitPos);
    int64 tell();
#endif

    std::streampos tellg();
    std::istream& seekg(std::streampos pos);
    std::istream& putback(char c);
    std::istream& unget();
};
```

Constructor behavior:

- For headered streams (`headerless == false`), metadata is read from the compressed stream. Constructor `entropy`, `transform`, `blockSize`, `checksum`, and `originalSize` are not authoritative after header decoding.
- For headerless streams (`headerless == true`), the caller must provide matching `entropy`, `transform`, `blockSize`, `checksum`, and bitstream version.

Methods:

| Method | Description |
| --- | --- |
| `read(data, length)` | Decodes up to `length` bytes into `data`. Sets `gcount()`. Throws on negative length, closed stream, invalid stream, checksum failure, or codec failure. |
| `gcount()` | Returns bytes decoded by the last `read()` or `get()`. |
| `get()` | Decodes and returns one byte, or `EOF`. |
| `peek()` | Returns next byte without consuming it, or `EOF`. |
| `close()` | Closes the compressed input stream and releases internal resources. |
| `getRead()` | Returns compressed bytes consumed so far. |
| `seek(bitPos)` | Seeks to a bit position. Valid positions are block boundaries. Returns false on invalid/closed stream or failed underlying seek. |
| `tell()` | Returns current bit position from the underlying bitstream. |
| `tellg()` | Not supported. Throws `std::ios_base::failure`. |
| `seekg(pos)` | Not supported. Throws `std::ios_base::failure`. |
| `putback(c)` | Not supported. Sets `badbit` and throws. |
| `unget()` | Not supported. Sets `badbit` and throws. |

### C++ Stream Example

```cpp
#include <fstream>
#include "io/CompressedOutputStream.hpp"
#include "io/CompressedInputStream.hpp"

void round_trip(const char* input_path, const char* compressed_path, const char* output_path) {
    {
        std::ifstream in(input_path, std::ios::binary);
        std::ofstream out(compressed_path, std::ios::binary);
        kanzi::CompressedOutputStream cos(out, 1, "ANS0", "LZ", 1 << 20, 32);

        char buffer[65536];
        while (in.good()) {
            in.read(buffer, sizeof(buffer));
            std::streamsize n = in.gcount();
            if (n > 0)
                cos.write(buffer, n);
        }

        cos.close();
    }

    {
        std::ifstream in(compressed_path, std::ios::binary);
        std::ofstream out(output_path, std::ios::binary);
        kanzi::CompressedInputStream cis(in, 1);

        char buffer[65536];
        while (true) {
            cis.read(buffer, sizeof(buffer));
            std::streamsize n = cis.gcount();
            if (n <= 0)
                break;
            out.write(buffer, n);
        }

        cis.close();
    }
}
```

## Context Keys

`kanzi::Context` is a string-keyed parameter map used by stream constructors, factories, transforms, and entropy codecs.

```cpp
class kanzi::Context {
public:
    bool has(const std::string& key) const;
    int getInt(const std::string& key, int defValue = 0) const;
    int64 getLong(const std::string& key, int64 defValue = 0) const;
    std::string getString(const std::string& key,
                          const std::string& defValue = "") const;
    void putInt(const std::string& key, int value);
    void putLong(const std::string& key, int64 value);
    void putString(const std::string& key, const std::string& value);

#ifdef CONCURRENCY_ENABLED
    ThreadPool* getPool() const;
#endif
};
```

Common stream keys:

| Key | Type | Used by | Description |
| --- | --- | --- | --- |
| `jobs` | int | Input/output streams, BWT, codecs | Concurrent jobs. |
| `blockSize` | int | Input/output streams, transforms, entropy predictors | Block size in bytes. |
| `checksum` | int | Input/output streams | `0`, `32`, or `64`. |
| `entropy` | string | Input/output streams, factories, transforms | Entropy codec name. |
| `transform` | string | Input/output streams, factories, transforms | Transform name or chain. |
| `bsVersion` | int | Input stream, output stream context, version-sensitive codecs | Bitstream version. Defaults to current version in headerless input mode when omitted. |
| `outputSize` | int64 | Headerless input stream | Optional original decoded size. |
| `size` | int | Some entropy predictors | Current block size hint. |
| `dataType` | int | Transforms | Internal detected data type passed between transforms. |
| `textcodec` | int | `TEXT` transform | Internal text codec selection. |
| `packOnlyDNA` | int | `DNA` transform | Internal flag for DNA-only packing. |

Only the stream keys should be considered stable integration inputs. Other keys are implementation details used by codec pipelines.

Context constructor requirements:

```cpp
kanzi::Context ctx;
ctx.putInt("jobs", 1);
ctx.putString("entropy", "ANS0");
ctx.putString("transform", "LZ");
ctx.putInt("blockSize", 1 << 20);
ctx.putInt("checksum", 32);

kanzi::CompressedOutputStream cos(out, ctx);
```

For headerless input:

```cpp
kanzi::Context ctx;
ctx.putInt("jobs", 1);
ctx.putString("entropy", "ANS0");
ctx.putString("transform", "LZ");
ctx.putInt("blockSize", 1 << 20);
ctx.putInt("checksum", 32);
ctx.putInt("bsVersion", 6);       // optional; defaults to current version
ctx.putLong("outputSize", size);  // optional

kanzi::CompressedInputStream cis(in, ctx, true);
```

## Transforms

Header:

```cpp
#include "Transform.hpp"
#include "transform/TransformFactory.hpp"
```

### `Transform<T>`

```cpp
template <class T>
class kanzi::Transform {
public:
    virtual bool forward(SliceArray<T>& src, SliceArray<T>& dst, int length) = 0;
    virtual bool inverse(SliceArray<T>& src, SliceArray<T>& dst, int length) = 0;
    virtual int getMaxEncodedLength(int srcLen) const = 0;
    virtual ~Transform();
};
```

Contract:

- `forward()` transforms `length` elements from `src` into `dst`.
- `inverse()` reverses `forward()`.
- Both methods update `SliceArray::_index` on success according to consumed/produced elements.
- Return `false` when the transform elects not to apply or cannot encode/decode the provided data.
- Throw `std::invalid_argument` for invalid slice arguments.
- Implementations must not retain cross-call state that changes output depending on prior blocks.

### `SliceArray<T>`

```cpp
template <class T>
class kanzi::SliceArray {
public:
    T* _array;
    int _length;
    int _index;

    SliceArray(T* arr, int len, int index = 0);
    static bool isValid(const SliceArray& sa);
};
```

`_length` is the total buffer capacity. `_index` is the current offset. `SliceArray` does not own the buffer memory.

### `TransformFactory<T>`

```cpp
template <class T>
class kanzi::TransformFactory {
public:
    static uint64 getType(const char* name);
    static uint64 getTypeToken(const char* name);
    static std::string getName(uint64 functionType);
    static TransformSequence<T>* newTransform(Context& ctx, uint64 functionType);
};
```

Methods:

| Method | Description |
| --- | --- |
| `getType(name)` | Parses a transform chain such as `BWT+RANK+ZRLT` into an encoded `uint64`. Throws on unknown transform or more than 8 transforms. |
| `getTypeToken(name)` | Parses a single transform token into its type id. |
| `getName(functionType)` | Converts an encoded transform chain back to canonical names. |
| `newTransform(ctx, functionType)` | Allocates a `TransformSequence<T>` for the encoded chain. Caller owns the returned pointer. |

### `TransformSequence<T>`

Header:

```cpp
#include "transform/TransformSequence.hpp"
```

`TransformSequence<T>` is itself a `Transform<T>` that applies up to 8 transforms in order for `forward()` and in reverse order for `inverse()`. When constructed with `deallocate = true`, it owns and deletes the transform pointers.

## Entropy Codecs

Headers:

```cpp
#include "EntropyEncoder.hpp"
#include "EntropyDecoder.hpp"
#include "entropy/EntropyEncoderFactory.hpp"
#include "entropy/EntropyDecoderFactory.hpp"
```

### `EntropyEncoder`

```cpp
class kanzi::EntropyEncoder {
public:
    virtual int encode(const byte block[], uint blkptr, uint len) = 0;
    virtual OutputBitStream& getBitStream() const = 0;
    virtual void dispose() = 0;
    virtual ~EntropyEncoder();
};
```

`encode()` writes `len` bytes from `block + blkptr` and returns the number of bytes processed. Call `dispose()` before destroying or abandoning an encoder.

### `EntropyDecoder`

```cpp
class kanzi::EntropyDecoder {
public:
    virtual int decode(byte block[], uint blkptr, uint len) = 0;
    virtual InputBitStream& getBitStream() const = 0;
    virtual void dispose() = 0;
    virtual ~EntropyDecoder();
};
```

`decode()` writes `len` decoded bytes to `block + blkptr` and returns the number of bytes processed. Call `dispose()` before destroying or abandoning a decoder.

### Entropy Factories

```cpp
class kanzi::EntropyEncoderFactory {
public:
    static EntropyEncoder* newEncoder(OutputBitStream& obs, Context& ctx, short entropyType);
    static const char* getName(short entropyType);
    static short getType(const char* name);
};

class kanzi::EntropyDecoderFactory {
public:
    static EntropyDecoder* newDecoder(InputBitStream& ibs, Context& ctx, short entropyType);
    static const char* getName(short entropyType);
    static short getType(const char* name);
};
```

Caller owns codecs returned by `newEncoder()` and `newDecoder()`.

Factory type constants:

| Constant | Name |
| --- | --- |
| `NONE_TYPE` | `NONE` |
| `HUFFMAN_TYPE` | `HUFFMAN` |
| `FPAQ_TYPE` | `FPAQ` |
| `RANGE_TYPE` | `RANGE` |
| `ANS0_TYPE` | `ANS0` |
| `ANS1_TYPE` | `ANS1` |
| `CM_TYPE` | `CM` |
| `TPAQ_TYPE` | `TPAQ` |
| `TPAQX_TYPE` | `TPAQX` |

## Bitstream API

Headers:

```cpp
#include "InputBitStream.hpp"
#include "OutputBitStream.hpp"
#include "bitstream/DefaultInputBitStream.hpp"
#include "bitstream/DefaultOutputBitStream.hpp"
```

### `InputBitStream`

```cpp
class kanzi::InputBitStream {
public:
    virtual int readBit() = 0;
    virtual uint64 readBits(uint length) = 0;
    virtual uint readBits(byte bits[], uint length) = 0;
    virtual void close() = 0;
    virtual uint64 read() const = 0;
    virtual bool hasMoreToRead() = 0;
    virtual ~InputBitStream();
};
```

`readBits(uint length)` accepts lengths in `[1..64]`.

### `OutputBitStream`

```cpp
class kanzi::OutputBitStream {
public:
    virtual void writeBit(int bit) = 0;
    virtual uint writeBits(uint64 bits, uint length) = 0;
    virtual uint writeBits(const byte bits[], uint length) = 0;
    virtual void close() = 0;
    virtual uint64 written() const = 0;
    virtual ~OutputBitStream();
};
```

`writeBits(uint64 bits, uint length)` accepts lengths in `[1..64]`; invalid lengths return 0 in the default implementation.

### Default Bitstreams

```cpp
class kanzi::DefaultInputBitStream : public InputBitStream, public Seekable {
public:
    DefaultInputBitStream(InputStream& is, uint bufferSize = 65536);
    int readBit();
    uint64 readBits(uint length);
    uint readBits(byte bits[], uint count);
    void close();
    uint64 read() const;
    bool hasMoreToRead();
    bool isClosed() const;
    int64 tell();
    bool seek(int64 pos);
};

class kanzi::DefaultOutputBitStream : public OutputBitStream, public Seekable {
public:
    DefaultOutputBitStream(OutputStream& os, uint bufferSize = 65536);
    void writeBit(int bit);
    uint writeBits(uint64 bits, uint length);
    uint writeBits(const byte bits[], uint length);
    void close();
    uint64 written() const;
    bool isClosed() const;
    int64 tell();
    bool seek(int64 pos);
};
```

Default bitstream buffer sizes must be:

- At least 1024 bytes
- At most 536,870,912 bytes
- A multiple of 8

`DefaultInputBitStream::seek()` accepts any non-negative bit position and consumes partial-byte bits after seeking. `DefaultOutputBitStream::seek()` only accepts byte-aligned bit positions.

## Events and Listeners

Headers:

```cpp
#include "Event.hpp"
#include "Listener.hpp"
```

Streams can notify caller-provided listeners during compression and decompression.

### `Listener<T>`

```cpp
template <class T>
class kanzi::Listener {
public:
    virtual void processEvent(const T& evt) = 0;
    virtual ~Listener();
};
```

Register with:

```cpp
cos.addListener(listener);
cis.addListener(listener);
```

### `Event`

```cpp
class kanzi::Event {
public:
    enum Type {
        COMPRESSION_START,
        COMPRESSION_END,
        BEFORE_TRANSFORM,
        AFTER_TRANSFORM,
        BEFORE_ENTROPY,
        AFTER_ENTROPY,
        DECOMPRESSION_START,
        DECOMPRESSION_END,
        AFTER_HEADER_DECODING,
        BLOCK_INFO
    };

    enum HashType {
        NO_HASH,
        SIZE_32,
        SIZE_64
    };

    struct HeaderInfo {
        std::string inputName;
        int bsVersion;
        int checksumSize;
        int blockSize;
        std::string entropyType;
        std::string transformType;
        int64 originalSize;
        int64 fileSize;
    };

    int getId() const;
    int64 getSize() const;
    Type getType() const;
    WallTimer::TimeData getTime() const;
    uint64 getHash() const;
    int64 getOffset() const;
    HashType getHashType() const;
    HeaderInfo* getInfo() const;
    std::string toString() const;
    std::string getTypeAsString() const;
};
```

Common event data:

| Getter | Meaning |
| --- | --- |
| `getType()` | Event kind. |
| `getId()` | Block id or event id, depending on type. |
| `getSize()` | Block size or stage size. |
| `getHash()` | Block checksum when enabled, otherwise 0. |
| `getHashType()` | No hash, 32-bit hash, or 64-bit hash. |
| `getOffset()` | Compressed block offset when available. |
| `getInfo()` | Header metadata for `AFTER_HEADER_DECODING`. |

Minimal listener:

```cpp
class MyListener : public kanzi::Listener<kanzi::Event> {
public:
    void processEvent(const kanzi::Event& evt) {
        std::cout << evt.toString() << std::endl;
    }
};
```

## Errors and Exceptions

### C Error Codes

`src/Error.hpp` defines:

| Code | Name |
| --- | --- |
| 1 | `ERR_MISSING_PARAM` |
| 2 | `ERR_BLOCK_SIZE` |
| 3 | `ERR_INVALID_CODEC` |
| 4 | `ERR_CREATE_COMPRESSOR` |
| 5 | `ERR_CREATE_DECOMPRESSOR` |
| 6 | `ERR_OUTPUT_IS_DIR` |
| 7 | `ERR_OVERWRITE_FILE` |
| 8 | `ERR_CREATE_FILE` |
| 9 | `ERR_CREATE_BITSTREAM` |
| 10 | `ERR_OPEN_FILE` |
| 11 | `ERR_READ_FILE` |
| 12 | `ERR_WRITE_FILE` |
| 13 | `ERR_PROCESS_BLOCK` |
| 14 | `ERR_CREATE_CODEC` |
| 15 | `ERR_INVALID_FILE` |
| 16 | `ERR_STREAM_VERSION` |
| 17 | `ERR_CREATE_STREAM` |
| 18 | `ERR_INVALID_PARAM` |
| 19 | `ERR_CRC_CHECK` |
| 20 | `ERR_RESERVED_NAME` |
| 127 | `ERR_UNKNOWN` |

### `IOException`

Header:

```cpp
#include "io/IOException.hpp"
```

```cpp
class kanzi::IOException : public std::runtime_error {
public:
    IOException(const std::string& msg);
    IOException(const std::string& msg, int error);
    int error() const;
};
```

`error()` returns a value from `kanzi::Error::ErrorCode`.

### `BitStreamException`

Header:

```cpp
#include "BitStreamException.hpp"
```

```cpp
class kanzi::BitStreamException : public std::runtime_error {
public:
    enum BitStreamStatus {
        UNDEFINED = 0,
        INPUT_OUTPUT = 1,
        END_OF_STREAM = 2,
        INVALID_STREAM = 3,
        STREAM_CLOSED = 4
    };

    BitStreamException(const std::string& msg);
    BitStreamException(const std::string& msg, int code);
    int error() const;
};
```

## Python Wrapper

Files:

- `src/api/kanzi_c_api.py`: low-level `ctypes` declarations
- `src/api/kanzi.py`: higher-level Python classes

The wrapper expects the Kanzi shared library to be loadable by name:

| Platform | Library name |
| --- | --- |
| Windows | `kanzi.dll` |
| macOS | `libkanzi.dylib` |
| Linux/Unix | `libkanzi.so` |

Ensure the shared library is in the dynamic loader path before importing.

### `KanziError`

```python
class KanziError(RuntimeError):
    pass
```

Raised when a wrapped C function returns a non-zero Kanzi error code.

### `Compressor`

```python
class Compressor:
    def __init__(
        self,
        dst_path,
        transform=b"LZ",
        entropy=b"Huffman",
        block_size=1 << 20,
        jobs=1,
        checksum=0,
        headerless=0,
    )

    def compress(self, data: bytes) -> int
    def close(self) -> int
```

Behavior:

- Opens `dst_path` for binary writing.
- Initializes the C compressor.
- `compress(data)` writes one block and returns bytes written by that call.
- `close()` disposes the C compressor, closes the file, and returns bytes flushed during close.
- Supports context-manager use.

Example:

```python
from kanzi import Compressor

with Compressor("sample.knz", transform=b"LZ", entropy=b"ANS0", checksum=32) as c:
    c.compress(b"hello kanzi")
```

### `Decompressor`

```python
class Decompressor:
    def __init__(
        self,
        src_path,
        buffer_size,
        jobs=1,
        headerless=0,
        **headerless_params,
    )

    def decompress_block(self, max_output: int) -> bytes
    def close(self)
```

Behavior:

- Opens `src_path` for binary reading.
- Initializes the C decompressor.
- `decompress_block(max_output)` returns up to `max_output` decoded bytes.
- An empty result indicates end of stream.
- Supports context-manager use.

Headerless parameters, when `headerless=1`:

| Python keyword | C field |
| --- | --- |
| `transform` | `dData.transform` |
| `entropy` | `dData.entropy` |
| `blockSize` | `dData.blockSize` |
| `originalSize` | `dData.originalSize` |
| `checksum` | `dData.checksum` |
| `bsVersion` | `dData.bsVersion` |

Example:

```python
from kanzi import Decompressor

out = bytearray()
with Decompressor("sample.knz", buffer_size=1 << 20) as d:
    while True:
        block = d.decompress_block(1 << 20)
        if not block:
            break
        out.extend(block)
```

## Integration Notes

- Prefer the C API for ABI-stable dynamic-library integration.
- Prefer the C++ stream API when embedding directly in C++ applications.
- Keep the underlying `FILE*`, `std::istream`, or `std::ostream` open while the Kanzi context/stream exists.
- Always dispose or close compressors to flush final bytes.
- Use headered streams unless another protocol stores Kanzi parameters.
- Match headerless decompression parameters exactly to compression parameters.
- Do not share one stream instance concurrently between threads.
- Transform and entropy objects returned by factories are heap-allocated; delete them when no longer needed unless ownership is transferred to `TransformSequence`.
- Check return codes in the C API and catch `kanzi::IOException`, `kanzi::BitStreamException`, `std::invalid_argument`, and `std::ios_base::failure` in C++ integrations.
