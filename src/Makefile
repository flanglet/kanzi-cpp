#CXX=clang++
LDFLAGS=-lpthread
OBJ_DIR = obj
TEST_OBJ_DIR=$(OBJ_DIR)/test
APP=kanzi

ifeq ($(CONCURRENCY_DISABLED), 1)
	CONCURRENCY_FLAG = -DCONCURRENCY_DISABLED
endif

ifeq ($(OS), Windows_NT)
	CXXFLAGS=-c -std=c++11 -Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic -march=native -fno-rtti $(CONCURRENCY_FLAG)
	LDFLAGS=-lpthread -static
	#LDFLAGS=-static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
else
	ARCH ?= $(shell uname -m)

	ifeq ($(ARCH), x86_64)
		CXXFLAGS=-c -std=c++14 -Wall -Wextra -O3 -fomit-frame-pointer -fPIC -DNDEBUG -pedantic -march=native -fno-rtti $(CONCURRENCY_FLAG)
	else
		CXXFLAGS=-c -std=c++17 -Wall -Wextra -O3 -fPIC -DNDEBUG -pedantic -fno-rtti $(CONCURRENCY_FLAG)
	endif
endif	

LIB_COMMON_SOURCES=Global.cpp \
	Event.cpp \
	entropy/EntropyUtils.cpp \
	entropy/HuffmanCommon.cpp \
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

LIB_COMP_SOURCES=api/Compressor.cpp \
	bitstream/DebugOutputBitStream.cpp \
	bitstream/DefaultOutputBitStream.cpp \
	io/CompressedOutputStream.cpp \
	entropy/ANSRangeEncoder.cpp \
	entropy/BinaryEntropyEncoder.cpp \
	entropy/ExpGolombEncoder.cpp \
	entropy/FPAQEncoder.cpp \
	entropy/HuffmanEncoder.cpp \
	entropy/RangeEncoder.cpp

LIB_DECOMP_SOURCES=api/Decompressor.cpp \
	bitstream/DebugInputBitStream.cpp \
	bitstream/DefaultInputBitStream.cpp \
	io/CompressedInputStream.cpp \
	entropy/ANSRangeDecoder.cpp \
	entropy/BinaryEntropyDecoder.cpp \
	entropy/ExpGolombDecoder.cpp \
	entropy/FPAQDecoder.cpp \
	entropy/HuffmanDecoder.cpp \
	entropy/RangeDecoder.cpp

LIB_SOURCES=$(LIB_COMMON_SOURCES) $(LIB_COMP_SOURCES) $(LIB_DECOMP_SOURCES)

# Define library object files
LIB_OBJECTS=$(filter-out $(OBJ_DIR)/test/%.o,$(LIB_COMMON_OBJECTS) $(LIB_COMP_OBJECTS) $(LIB_DECOMP_OBJECTS))


TEST_SOURCES=test/TestEntropyCodec.cpp \
	test/TestBWT.cpp \
	test/TestCompressedStream.cpp \
	test/TestDefaultBitStream.cpp \
	test/TestTransforms.cpp

APP_SOURCES=app/Kanzi.cpp \
	app/InfoPrinter.cpp \
	app/BlockCompressor.cpp \
	app/BlockDecompressor.cpp

SOURCES=$(LIB_SOURCES) $(APP_SOURCES)

# Function to create object file paths, preserving the directory structure
OBJ = $(OBJ_DIR)/$(patsubst %.cpp,%.o,$(1))

LIB_COMMON_OBJECTS=$(foreach src,$(LIB_COMMON_SOURCES),$(call OBJ,$(src)))
LIB_COMP_OBJECTS=$(foreach src,$(LIB_COMP_SOURCES),$(call OBJ,$(src)))
LIB_DECOMP_OBJECTS=$(foreach src,$(LIB_DECOMP_SOURCES),$(call OBJ,$(src)))
LIB_OBJECTS=$(LIB_COMMON_OBJECTS) $(LIB_COMP_OBJECTS) $(LIB_DECOMP_OBJECTS)
#TEST_OBJECTS=$(foreach src,$(TEST_SOURCES),$(call OBJ,$(src)))
APP_OBJECTS=$(foreach src,$(APP_SOURCES),$(call OBJ,$(src)))
OBJECTS=$(LIB_OBJECTS) $(APP_OBJECTS) $(TEST_OBJECTS)
RPTS=$(SOURCES:.cpp=.optrpt)

	
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
STATIC_COMP_LIB := lib$(APP)comp$(STATIC_LIB_SUFFIX)
STATIC_DECOMP_LIB := lib$(APP)decomp$(STATIC_LIB_SUFFIX)
SHARED_COMP_LIB := lib$(APP)comp$(SHARED_LIB_SUFFIX)
SHARED_DECOMP_LIB := lib$(APP)decomp$(SHARED_LIB_SUFFIX)

libcomp: $(STATIC_COMP_LIB) $(SHARED_COMP_LIB)

libdecomp: $(STATIC_DECOMP_LIB) $(SHARED_DECOMP_LIB)

lib: $(STATIC_LIB) $(SHARED_LIB)

# Create static libraries
$(STATIC_LIB):$(LIB_OBJECTS)
	$(AR) cr ../lib/$@ $+

$(STATIC_COMP_LIB):$(LIB_COMP_OBJECTS)
	$(AR) cr ../lib/$@ $+

$(STATIC_DECOMP_LIB):$(LIB_DECOMP_OBJECTS)
	$(AR) cr ../lib/$@ $+

# Create shared libraries
$(SHARED_LIB):$(LIB_OBJECTS)
	$(CXX) -o ../lib/$@ $(LDFLAGS) -shared $+

$(SHARED_COMP_LIB):$(LIB_COMP_OBJECTS)
	$(CXX) -o ../lib/$@ $(LDFLAGS) -shared $+

$(SHARED_DECOMP_LIB):$(LIB_DECOMP_OBJECTS)
	$(CXX) -o ../lib/$@ $(LDFLAGS) -shared $+


# Targets for test executables
testBWT: $(LIB_OBJECTS) $(TEST_OBJ_DIR)/TestBWT.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testTransforms: $(LIB_OBJECTS) $(TEST_OBJ_DIR)/TestTransforms.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testEntropyCodec: $(LIB_OBJECTS) $(TEST_OBJ_DIR)/TestEntropyCodec.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testDefaultBitStream: $(LIB_OBJECTS) $(TEST_OBJ_DIR)/TestDefaultBitStream.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

testCompressedStream: $(LIB_OBJECTS) $(TEST_OBJ_DIR)/TestCompressedStream.o
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

test: testBWT testTransforms testEntropyCodec testDefaultBitStream testCompressedStream

# Target for the main application
kanzi: $(LIB_OBJECTS) $(APP_OBJECTS)
	$(CXX) $^ -o ../bin/$@ $(LDFLAGS)

all: lib test $(APP)

# Install / uninstall (may require sudo or admin rights)
ifeq ($(OS),Windows_NT)
	INSTALL_DIR="C:\Program Files"
else
	INSTALL_DIR=/usr/local
endif

install: $(STATIC_LIB) $(SHARED_LIB) $(APP)
ifeq ($(OS),Windows_NT)
	copy /Y ..\bin\$(APP)$(PROG_SUFFIX) $(INSTALL_DIR)
else
	install -d $(INSTALL_DIR)/lib
	install -m 644 ../lib/$(STATIC_LIB) $(INSTALL_DIR)/lib
	install -m 644 ../lib/$(SHARED_LIB) $(INSTALL_DIR)/lib
	install -d $(INSTALL_DIR)/include/kanzi
	install -m 644 ./*.hpp $(INSTALL_DIR)/include/kanzi
	install -d $(INSTALL_DIR)/include/kanzi/api
	install -m 644 ./api/*.hpp $(INSTALL_DIR)/include/kanzi/api
	install -d $(INSTALL_DIR)/include/kanzi/io
	install -m 644 ./io/*.hpp $(INSTALL_DIR)/include/kanzi/io
	install -d $(INSTALL_DIR)/include/kanzi/entropy
	install -m 644 ./entropy/*.hpp $(INSTALL_DIR)/include/kanzi/entropy
	install -d $(INSTALL_DIR)/include/kanzi/bitstream
	install -m 644 ./bitstream/*.hpp $(INSTALL_DIR)/include/kanzi/bitstream
	install -d $(INSTALL_DIR)/include/kanzi/transform
	install -m 644 ./transform/*.hpp $(INSTALL_DIR)/include/kanzi/transform
	install -d $(INSTALL_DIR)/include/kanzi/util
	install -m 644 ./util/*.hpp $(INSTALL_DIR)/include/kanzi/util
	install -d $(INSTALL_DIR)/bin
	install -m577 ../bin/$(APP)$(PROG_SUFFIX) $(INSTALL_DIR)/bin
endif

# Uninstall may require sudo or admin rights
uninstall:
ifeq ($(OS),Windows_NT)
	del /Q $(INSTALL_DIR)\$(APP)$(PROG_SUFFIX)
else
	rm -f -r $(INSTALL_DIR)/include/kanzi
	rm -f $(INSTALL_DIR)/lib/$(STATIC_LIB)
	rm -f $(INSTALL_DIR)/lib/$(SHARED_LIB)
	rm -f $(INSTALL_DIR)/bin/$(APP)$(PROG_SUFFIX)
endif

clean:
ifeq ($(OS),Windows_NT)
	del /S $(OBJ_DIR)\*.o ..\bin\$(APP)$(PROG_SUFFIX) ..\bin\test*$(PROG_SUFFIX) \
	..\lib\$(STATIC_LIB) ..\lib\$(SHARED_LIB) \
	..\lib\$(STATIC_COMP_LIB) ..\lib\$(STATIC_DECOMP_LIB) \
	..\lib\$(SHARED_COMP_LIB) ..\lib\$(SHARED_DECOMP_LIB)
else
	rm -f ../bin/test*$(PROG_SUFFIX) $(OBJECTS) $(RPTS) \
	../bin/$(APP)$(PROG_SUFFIX) ../lib/$(STATIC_LIB) ../lib/$(SHARED_LIB) \
	../lib/$(STATIC_COMP_LIB) ../lib/$(STATIC_DECOMP_LIB) \
	../lib/$(SHARED_COMP_LIB) ../lib/$(SHARED_DECOMP_LIB)
	rm -rf $(OBJ_DIR)
endif

ifeq ($(OS),Windows_NT)
   MKDIR = if not exist $(subst /,\,$(dir $@)) mkdir $(subst /,\,$(dir $@))
else
   MKDIR = mkdir -p $(dir $@)
endif

$(OBJ_DIR)/%.o: %.cpp
	@$(MKDIR)
	$(CXX) $(CXXFLAGS) $< -o $@

