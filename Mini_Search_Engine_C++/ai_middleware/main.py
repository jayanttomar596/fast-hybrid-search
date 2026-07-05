from fastapi import Request
from fastapi.responses import Response
import requests

# ... (Your existing /api/chat and /api/embeddings routes stay exactly as they are) ...

# 3. TRAFFIC COP: PROXY ALL OTHER /api/ REQUESTS TO C++
@app.api_route("/api/{path:path}", methods=["GET", "POST", "OPTIONS"])
async def proxy_to_cpp(path: str, request: Request):
    # Do not intercept chat or embeddings, Python handles those!
    if path in ["chat", "embeddings"]:
        return
        
    cpp_url = f"http://localhost:8080/{path}"
    
    # Grab the URL parameters (like ?q=... ) and the body (for file uploads)
    params = dict(request.query_params)
    body = await request.body()
    
    # Clean the headers so the internal network doesn't get confused
    headers = dict(request.headers)
    headers.pop("host", None) 
    
    try:
        # Forward the exact request to the C++ backend
        resp = requests.request(
            method=request.method,
            url=cpp_url,
            params=params,
            data=body,
            headers=headers
        )
        return Response(content=resp.content, status_code=resp.status_code, headers=dict(resp.headers))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"C++ Engine unreachable: {str(e)}")

# 4. SERVE THE FRONTEND
app.mount("/", StaticFiles(directory="../frontend", html=True), name="frontend")

# ... (if __name__ == "__main__": stays here) ...