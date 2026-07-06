#!/bin/bash

# 1. Start C++ Engine in background
cd /app/backend
./engine > /var/log/cpp_engine.log 2>&1 &

# 2. Wait longer for the engine to load the index into memory
echo "Waiting for C++ Engine index to load..."
sleep 15 

# 3. Start Python Gateway
cd /app/ai_middleware
python3 -m uvicorn main:app --host 0.0.0.0 --port $PORT