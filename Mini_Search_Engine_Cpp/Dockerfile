# Build C++ Engine and Setup Python
FROM ubuntu:22.04

# Prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install C++ tools, Python 3.10, PDF parsers, and OpenSSL headers
RUN apt-get update && apt-get install -y \
    build-essential \
    python3.10 \
    python3-pip \
    poppler-utils \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Set up working directory
WORKDIR /app

# Copy the entire project into the container
COPY . /app

# 1. Compile the C++ Engine (Added -lssl and -lcrypto for OpenSSL)
WORKDIR /app/backend
RUN g++ -std=c++17 -O3 server.cpp src/SearchEngine.cpp src/Trie.cpp -Iinclude -o engine -lpthread -lssl -lcrypto

# 2. Install Python Dependencies
WORKDIR /app/ai_middleware
RUN pip install --no-cache-dir -r requirements.txt

# 3. Create storage folders
RUN mkdir -p /app/runtime_corpus /app/database && \
    chmod -R 777 /app/runtime_corpus /app/database

# Make our startup script executable
WORKDIR /app
RUN chmod +x start.sh

# Render injects the $PORT variable dynamically
CMD ["./start.sh"]