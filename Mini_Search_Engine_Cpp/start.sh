#!/bin/bash

# 1. Start C++ Engine
cd /app/backend
./engine &

# 2. Wait 5 seconds for the C++ engine to boot up
sleep 5

# 3. Start Python Gateway
cd /app/ai_middleware
python3 -m uvicorn main:app --host 0.0.0.0 --port $PORT