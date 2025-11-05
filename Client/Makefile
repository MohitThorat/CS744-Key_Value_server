# Compiler for C++
CXX = g++

# Compiler flags
# -std=c++17 is needed for modern C++ features
# -O2 enables optimizations (makes your loadgen faster)
# -Wall shows all warnings
CXXFLAGS = -std=c++17 -O2 -Wall

# Linker libraries
# -lcurl for the cURL library
# -lpthread for std::thread
LDLIBS = -lcurl -lpthread

# Source file
SRC = load_gen.cpp

# Target executable name
TARGET = load_gen

# --- RULES ---

# Default rule: build the executable
all: $(TARGET)

# Rule to compile and link the final target
$(TARGET): $(SRC)
	@echo "Compiling and linking $(TARGET)..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# 'make clean' rule
.PHONY: clean all
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET)