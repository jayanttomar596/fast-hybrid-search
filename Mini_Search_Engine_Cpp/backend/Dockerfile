# Build stage
FROM ubuntu:22.04 AS builder

# Install build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy backend source code
COPY backend/include ./include
COPY backend/src ./src
COPY backend/server.cpp ./

# Compile the C++ executable
RUN g++ -std=c++17 -O3 -I./include src/SearchEngine.cpp src/Trie.cpp server.cpp -o search_engine -lpthread

# Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies (poppler-utils for PDF processing)
RUN apt-get update && apt-get install -y \
    poppler-utils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the compiled binary from the builder stage
COPY --from=builder /app/search_engine .

# Ensure standard runtime directories exist
RUN mkdir -p documents runtime_corpus

EXPOSE 8080

# Run the server
CMD ["./search_engine"]