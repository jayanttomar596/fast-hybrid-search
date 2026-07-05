# Compiler and language standard
CXX = g++
CXXFLAGS = -std=c++17 -Wall

# Include paths (Project includes + Homebrew OpenSSL includes)
INCLUDES = -I./include -I/opt/homebrew/opt/openssl@3/include

# Library paths and linker flags (Homebrew OpenSSL libs + multithreading)
LDFLAGS = -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -pthread

# Source files and output binary
SRCS = server.cpp src/SearchEngine.cpp src/Trie.cpp
TARGET = server

# Default target runs when you just type 'make'
all: $(TARGET)

# Compile the target
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET) $(LDFLAGS)

# Run the server
run: $(TARGET)
	./$(TARGET)

# Clean up compiled files
clean:
	rm -f $(TARGET)