CXX=g++

ifeq ($(OS),Windows_NT)
	CFLAGS=-c -std=c++11 -Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic -march=native -fno-rtti
	#LDFLAGS=-static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
	LDFLAGS=-static-libgcc -static-libstdc++ -lpthread
else
	LDFLAGS=-lpthread
	ARCH := $(shell uname -m)

	ifeq ($(ARCH),x86_64)
		CFLAGS=-c -std=c++14 -Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic -march=native -fno-rtti
	else
		CFLAGS=-c -std=c++14 -Wall -Wextra -O3 -fPIC -DNDEBUG -pedantic -fno-rtti
	endif
endif	

LIB_SOURCES=Global.cpp \
	Event.cpp \
	api/libapi.cpp \
	bitstream/DebugInputBitStream.cpp \
	bitstream/DebugOutputBitStream.cpp \
	bitstream/DefaultInputBitStream.cpp \
	bitstream/DefaultOutputBitStream.cpp \
	io/CompressedInputStream.cpp \
	io/CompressedOutputStream.cpp \
	entropy/ANSRangeDecoder.cpp \
	entropy/ANSRangeEncoder.cpp \
	entropy/BinaryEntropyDecoder.cpp \
	entropy/BinaryEntropyEncoder.cpp \
	entropy/EntropyUtils.cpp \
	entropy/ExpGolombDecoder.cpp \
	entropy/ExpGolombEncoder.cpp \
	entropy/HuffmanCommon.cpp \
	entropy/HuffmanDecoder.cpp \
	entropy/HuffmanEncoder.cpp \
	entropy/RangeDecoder.cpp \
	entropy/RangeEncoder.cpp \
	entropy/RiceGolombDecoder.cpp \
	entropy/RiceGolombEncoder.cpp \
	entropy/FPAQDecoder.cpp \
	entropy/FPAQEncoder.cpp \
	entropy/CMPredictor.cpp \
	entropy/TPAQPredictor.cpp \
	transform/AliasCodec.cpp \
	transform/BWT.cpp \
	transform/BWTS.cpp \
	transform/DivSufSort.cpp \
	transform/SBRT.cpp \
	transform/BWTBlockCodec.cpp \
	transform/LZCodec.cpp \
	transform/FSDCodec.cpp \
	transform/ROLZCodec.cpp \
	transform/RLT.cpp \
	transform/SRT.cpp \
	transform/TextCodec.cpp \
	transform/UTFCodec.cpp \
	transform/EXECodec.cpp \
	transform/ZRLT.cpp
LIB_OBJECTS=$(LIB_SOURCES:.cpp=.o)

TEST_SOURCES=test/TestEntropyCodec.cpp \
	test/TestBWT.cpp \
	test/TestCompressedStream.cpp \
	test/TestDefaultBitStream.cpp \
	test/TestTransforms.cpp 
TEST_OBJECTS=$(TEST_SOURCES:.cpp=.o)

APP_SOURCES=app/Kanzi.cpp \
	app/InfoPrinter.cpp \
	app/BlockCompressor.cpp \
	app/BlockDecompressor.cpp
APP_OBJECTS=$(APP_SOURCES:.cpp=.o)

SOURCES=$(LIB_SOURCES) $(APP_SOURCES)
OBJECTS=$(SOURCES:.cpp=.o)
RPTS=$(SOURCES:.cpp=.optrpt)
TESTS=testBWT testTransforms \
	testEntropyCodec testDefaultBitStream \
	testCompressedStream
	
APP=kanzi
	
STATIC_LIB_SUFFIX := .a
SHARED_LIB_SUFFIX := .so
PROG_SUFFIX       := 

# Compiling for Windows with MinGW?
ifeq ($(OS),Windows_NT)
    STATIC_LIB_SUFFIX := .lib
    SHARED_LIB_SUFFIX := .dll
    PROG_SUFFIX       := .exe
endif

STATIC_LIB := lib$(APP)$(STATIC_LIB_SUFFIX)
SHARED_LIB := lib$(APP)$(SHARED_LIB_SUFFIX)

all: $(STATIC_LIB) $(SHARED_LIB) $(APP) $(TESTS)

# Create static library
$(STATIC_LIB):$(LIB_OBJECTS)
	$(AR) cr ../lib/$@ $+

# Create shared library
$(SHARED_LIB):$(LIB_OBJECTS)
	$(CXX) -o ../lib/$@ $(LDFLAGS) -shared $+


testBWT: $(LIB_OBJECTS) test/TestBWT.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testTransforms: $(LIB_OBJECTS) test/TestTransforms.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testEntropyCodec: $(LIB_OBJECTS) test/TestEntropyCodec.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testDefaultBitStream: $(LIB_OBJECTS) test/TestDefaultBitStream.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testCompressedStream: $(LIB_OBJECTS) test/TestCompressedStream.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

kanzi: $(OBJECTS) app/Kanzi.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

clean: 
ifeq ($(OS),Windows_NT)
	del /S *.o *.obj ..\bin\$(APP)$(PROG_SUFFIX) ..\lib\$(STATIC_LIB) ..\lib\$(SHARED_LIB)
else
	rm -f ../bin/test*$(PROG_SUFFIX) $(OBJECTS) $(RPTS) $(TEST_OBJECTS) \
        ../bin/$(APP)$(PROG_SUFFIX) ../lib/$(STATIC_LIB) ../lib/$(SHARED_LIB)
endif

.cpp.o:
	$(CXX) $(CFLAGS) $< -o $@
