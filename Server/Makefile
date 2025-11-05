# Compiler for C++
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2 -Dnum_thread -Wno-deprecated-declarations

# Compiler for C (for civetweb.c)
CC = gcc
CFLAGS = -Wall -O2 -Dnum_thread -DOPENSSL_API_3_0 -DNO_SSL_DL

# Linker libraries
LDLIBS = -lmysqlclient -lpthread -lcrypto -lssl -ldl

# --- NEW STRUCTURE ---

# Directories
BUILD_DIR = build
SRC_DIR = src
DEPS_DIR = third_party
INC_DIR = include

# Tell 'make' where to find source files
VPATH = $(SRC_DIR):$(DEPS_DIR)/civetweb

# Tell compilers where to find header files
INC_PATHS = -I$(INC_DIR) -I$(DEPS_DIR)/civetweb -I$(DEPS_DIR)
CXXFLAGS += $(INC_PATHS)
CFLAGS += $(INC_PATHS)

# Final executable name
TARGET = server

# List of OBJECT files (not sources)
OBJ_FILES = server.o LRUCache.o MySQLHelper.o MySQLPool.o CivetServer.o civetweb.o

# Add the build directory prefix to all object files
OBJ = $(addprefix $(BUILD_DIR)/, $(OBJ_FILES))

# --- RULES ---

# Default rule: build the executable
all: $(BUILD_DIR)/$(TARGET)

# Rule to link the final target
$(BUILD_DIR)/$(TARGET): $(OBJ)
	@echo "Linking $(TARGET)..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# Rules for compiling C++ and C files
# This pattern puts all .o files into $(BUILD_DIR)
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to create the build directory
$(BUILD_DIR):
	@mkdir -p $@

# 'make clean' rule
.PHONY: clean all
clean:
	@echo "Cleaning up..."
	rm -rf $(BUILD_DIR)