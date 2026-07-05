#!/bin/bash

# 1. Start the C++ Engine in the background (runs on internal port 8080)
echo "Starting C++ Engine..."
cd /app/backend
./engine &

# 2. Start the Python AI Gateway in the foreground (runs on Render's web port)
echo "Starting Python Gateway..."
cd /app/ai_middleware
# We use the $PORT variable that Render provides automatically
python3 -m uvicorn main:app --host 0.0.0.0 --port $PORT