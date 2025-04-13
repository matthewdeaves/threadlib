# Makefile for ThreadLib Classic Mac examples using Retro68
# Builds ThreadsTest and ThreadsTimed applications.
# Outputs are generated in the build/ directory.

# --- Variables ---

# Assume Retro68 tools are in the PATH
CC_MAC = m68k-apple-macos-gcc
CXX_MAC = m68k-apple-macos-g++ # Variable still defined, but not used for linking below
REZ = Rez

# --- Dynamically Find RIncludes and CIncludes ---

# Find the full path to the compiler
CC_MAC_PATH = $(shell which $(CC_MAC))

# Check if the compiler was found in the PATH
ifeq ($(CC_MAC_PATH),)
    $(error "$(CC_MAC) not found in PATH. Please ensure the Retro68 toolchain is installed and its bin directory is added to your PATH environment variable.")
endif

# Derive the toolchain directory (assuming structure like .../Retro68-build/toolchain/bin/compiler)
TOOLCHAIN_BIN_DIR = $(shell dirname $(CC_MAC_PATH))
TOOLCHAIN_DIR = $(shell dirname $(TOOLCHAIN_BIN_DIR))

# Construct the expected path to RIncludes relative to the toolchain directory
RINCLUDES = $(TOOLCHAIN_DIR)/m68k-apple-macos/RIncludes

# Check if the derived RIncludes directory actually exists
ifeq ($(wildcard $(RINCLUDES)/.),) # Check for existence of the directory itself
    $(error "Retro68 RIncludes directory not found at the expected location relative to the compiler: $(RINCLUDES). Check your Retro68 installation.")
endif

# Construct and Check C Includes Path
CINCLUDES = $(TOOLCHAIN_DIR)/m68k-apple-macos/include
ifeq ($(wildcard $(CINCLUDES)/.),)
    $(error "Retro68 CIncludes directory not found at the expected location relative to the compiler: $(CINCLUDES). Check your Retro68 installation.")
endif

# Construct Universal C Includes Path
UNIVERSAL_CINCLUDES = $(TOOLCHAIN_DIR)/universal/CIncludes
ifeq ($(wildcard $(UNIVERSAL_CINCLUDES)/.),)
    $(error "Retro68 Universal CIncludes directory not found at the expected location relative to the toolchain: $(UNIVERSAL_CINCLUDES). Check your Retro68 installation.")
endif
# --- END Path Finding Section ---


# --- Compiler, Linker, Rez Flags ---

# Compiler flags
# Added -IThreadLib so application code can find ThreadLib.h
CFLAGS_MAC = -g -w -ffunction-sections -D__MACOS__ -IThreadLib -I"$(CINCLUDES)" -I"$(UNIVERSAL_CINCLUDES)"

# Linker flags (passed via the compiler driver using -Wl,)
# Reverted to simple flags, assuming SetIText issue resolved in code
LDFLAGS_MAC = -Wl,-gc-sections -Wl,--mac-strip-macsbug

# Rez flags
REZFLAGS = -I"$(RINCLUDES)" # Quote the path

# --- Source and Build Directories ---

LIB_DIR = ThreadLib
TEST_DIR = ThreadsTest
TIMED_DIR = ThreadsTimed

BUILD_BASE_DIR = build
OBJ_BASE_DIR = $(BUILD_BASE_DIR)/obj

# Specific build/obj dirs for each target
BUILD_DIR_TEST = $(BUILD_BASE_DIR)/ThreadsTest
OBJ_DIR_TEST = $(OBJ_BASE_DIR)/ThreadsTest

BUILD_DIR_TIMED = $(BUILD_BASE_DIR)/ThreadsTimed
OBJ_DIR_TIMED = $(OBJ_BASE_DIR)/ThreadsTimed

# Object dir for the shared library code
OBJ_DIR_LIB = $(OBJ_BASE_DIR)/lib

# --- Source Files ---

LIB_C_FILES = $(wildcard $(LIB_DIR)/*.c)
LIB_H_FILES = $(wildcard $(LIB_DIR)/*.h) # For dependency tracking if needed

TEST_C_FILES = $(wildcard $(TEST_DIR)/*.c)
TEST_R_FILE = $(TEST_DIR)/ThreadsTest.r

TIMED_C_FILES = $(wildcard $(TIMED_DIR)/*.c)
# ThreadsTimed does not have its own .r file, uses Retro68APPL.r

# --- Object Files ---
# Place library objects in their own subdirectory
LIB_OBJS = $(patsubst $(LIB_DIR)/%.c, $(OBJ_DIR_LIB)/%.o, $(LIB_C_FILES))

# Place application objects in their respective subdirectories
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(OBJ_DIR_TEST)/%.o, $(TEST_C_FILES))
TIMED_OBJS = $(patsubst $(TIMED_DIR)/%.c, $(OBJ_DIR_TIMED)/%.o, $(TIMED_C_FILES))

# --- Resource and Target Definitions ---

# ThreadsTest App
TEST_INTERMEDIATE = $(OBJ_DIR_TEST)/ThreadsTest.code.bin
TEST_TARGET_BASE = $(BUILD_DIR_TEST)/ThreadsTest
TEST_TARGET_APPL = $(TEST_TARGET_BASE).APPL
TEST_TARGET_BIN = $(TEST_TARGET_BASE).bin
TEST_TARGET_DSK = $(TEST_TARGET_BASE).dsk
TEST_FINAL_TARGETS = $(TEST_TARGET_APPL) $(TEST_TARGET_BIN) $(TEST_TARGET_DSK)

# ThreadsTimed App
TIMED_INTERMEDIATE = $(OBJ_DIR_TIMED)/ThreadsTimed.code.bin
TIMED_TARGET_BASE = $(BUILD_DIR_TIMED)/ThreadsTimed
TIMED_TARGET_APPL = $(TIMED_TARGET_BASE).APPL
TIMED_TARGET_BIN = $(TIMED_TARGET_BASE).bin
TIMED_TARGET_DSK = $(TIMED_TARGET_BASE).dsk
TIMED_FINAL_TARGETS = $(TIMED_TARGET_APPL) $(TIMED_TARGET_BIN) $(TIMED_TARGET_DSK)

# --- Targets ---

# Default target: Build both applications
all: threadstest threadstimed

# Target to build just ThreadsTest
threadstest: $(TEST_FINAL_TARGETS)

# Target to build just ThreadsTimed
threadstimed: $(TIMED_FINAL_TARGETS)

# --- Build Rules ---

# == ThreadsTest Application ==

# Rule to create the final ThreadsTest outputs using Rez
$(TEST_FINAL_TARGETS): $(TEST_INTERMEDIATE) $(TEST_R_FILE) Makefile | $(BUILD_DIR_TEST)
	@echo "--- Rez Stage (ThreadsTest) ---"
	$(REZ) $(REZFLAGS) \
		--copy "$(TEST_INTERMEDIATE)" \
		"$(RINCLUDES)/Retro68APPL.r" \
		"$(TEST_R_FILE)" \
		-t "APPL" -c "TsTA" \
		-o "$(TEST_TARGET_BIN)" --cc "$(TEST_TARGET_APPL)" --cc "$(TEST_TARGET_DSK)"
	@echo "ThreadsTest build complete. Outputs in $(BUILD_DIR_TEST)"

# Rule to link the intermediate ThreadsTest code binary
# Depends on its own objects AND the library objects
$(TEST_INTERMEDIATE): $(TEST_OBJS) $(LIB_OBJS) Makefile | $(OBJ_DIR_TEST)
	@echo "--- Link Stage (ThreadsTest) ---"
	$(CC_MAC) $(TEST_OBJS) $(LIB_OBJS) -o $@ $(LDFLAGS_MAC) # Using CC_MAC

# Rule to compile ThreadsTest C source files
$(OBJ_DIR_TEST)/%.o: $(TEST_DIR)/%.c $(LIB_H_FILES) Makefile | $(OBJ_DIR_TEST)
	@echo "--- Compile Stage (ThreadsTest) ---"
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@


# == ThreadsTimed Application ==

# Rule to create the final ThreadsTimed outputs using Rez
# Note: Uses only Retro68APPL.r, no custom .r file
$(TIMED_FINAL_TARGETS): $(TIMED_INTERMEDIATE) Makefile | $(BUILD_DIR_TIMED)
	@echo "--- Rez Stage (ThreadsTimed) ---"
	$(REZ) $(REZFLAGS) \
		--copy "$(TIMED_INTERMEDIATE)" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "TimA" \
		-o "$(TIMED_TARGET_BIN)" --cc "$(TIMED_TARGET_APPL)" --cc "$(TIMED_TARGET_DSK)"
	@echo "ThreadsTimed build complete. Outputs in $(BUILD_DIR_TIMED)"

# Rule to link the intermediate ThreadsTimed code binary
# Depends on its own objects AND the library objects
$(TIMED_INTERMEDIATE): $(TIMED_OBJS) $(LIB_OBJS) Makefile | $(OBJ_DIR_TIMED)
	@echo "--- Link Stage (ThreadsTimed) ---"
	$(CC_MAC) $(TIMED_OBJS) $(LIB_OBJS) -o $@ $(LDFLAGS_MAC) # Using CC_MAC

# Rule to compile ThreadsTimed C source files
$(OBJ_DIR_TIMED)/%.o: $(TIMED_DIR)/%.c $(LIB_H_FILES) Makefile | $(OBJ_DIR_TIMED)
	@echo "--- Compile Stage (ThreadsTimed) ---"
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@


# == Shared Library Code ==

# Rule to compile shared ThreadLib C source files into object files in OBJ_DIR_LIB
$(OBJ_DIR_LIB)/%.o: $(LIB_DIR)/%.c $(LIB_H_FILES) Makefile | $(OBJ_DIR_LIB)
	@echo "--- Compile Stage (ThreadLib) ---"
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@


# == Directory Creation ==

# Rule to create the build and object directories if they don't exist
$(BUILD_DIR_TEST) $(OBJ_DIR_TEST) $(BUILD_DIR_TIMED) $(OBJ_DIR_TIMED) $(OBJ_DIR_LIB):
	@mkdir -p $@

# --- Cleanup ---

# Target to clean up all build files
clean:
	@echo "Cleaning all build files..."
	@rm -rf $(BUILD_BASE_DIR) # Remove the whole build directory
	@echo "Clean complete."

# Phony targets are not files
.PHONY: all clean threadstest threadstimed