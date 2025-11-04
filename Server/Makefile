# Compiler for C++
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2 -Dnum_thread -Wno-deprecated-declarations

# Compiler for C (for civetweb.c)
CC = gcc
# Added -DNO_SSL_DL to disable dynamic OpenSSL loading
CFLAGS = -Wall -O2 -Dnum_thread -DOPENSSL_API_3_0 -DNO_SSL_DL

# Linker flags: Added -lssl (needed by CivetWeb) and kept -ldl
LDFLAGS = -lmysqlclient -lpthread -lcrypto -lssl -ldl

# List all .o files explicitly
OBJ = server.o LRUCache.o MySQLHelper.o MySQLPool.o CivetServer.o civetweb.o
TARGET = server

# Rule to link the final target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Rule for compiling C++ (.cpp) files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule for compiling C (.c) files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
