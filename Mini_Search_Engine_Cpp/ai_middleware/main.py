import os
import uvicorn
import requests
import numpy as np
from collections import deque
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from langchain_google_genai import ChatGoogleGenerativeAI, GoogleGenerativeAIEmbeddings
from langchain_core.prompts import PromptTemplate

# 1. INITIALIZE APP FIRST
app = FastAPI(title="RAG Orchestration API")

# 2. CONFIGURE MIDDLEWARE
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# 3. SETUP MODELS
# Ensure GOOGLE_API_KEY is set in Render Environment Variables
embedder = GoogleGenerativeAIEmbeddings(model="models/text-embedding-004")
llm = ChatGoogleGenerativeAI(model="gemini-1.5-flash", temperature=0)

MAX_CACHE_SIZE = 100
semantic_cache = deque(maxlen=MAX_CACHE_SIZE)
SIMILARITY_THRESHOLD = 0.95

class ChatRequest(BaseModel):
    question: str

# 4. TRAFFIC COP: PROXY ALL OTHER /api/ REQUESTS TO C++
@app.api_route("/api/{path:path}", methods=["GET", "POST", "OPTIONS"])
async def proxy_to_cpp(path: str, request: Request):
    # Do not intercept chat or embeddings, Python handles those!
    if path in ["chat", "embeddings"]:
        return
        
    cpp_url = f"http://localhost:8080/{path}"
    
    # Grab URL parameters and body
    params = dict(request.query_params)
    body = await request.body()
    
    # Clean headers
    headers = dict(request.headers)
    headers.pop("host", None) 
    
    try:
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

# 5. CHAT ENDPOINT
@app.post("/api/chat")
def rag_chat(request: ChatRequest):
    question_vector = np.array(embedder.embed_query(request.question))
    
    for entry in semantic_cache:
        similarity = np.dot(question_vector, entry["vector"]) / (
            np.linalg.norm(question_vector) * np.linalg.norm(entry["vector"])
        )
        if similarity > SIMILARITY_THRESHOLD:
            return {"answer": entry["answer"], "cached": True}

    try:
        cpp_response = requests.get(f"http://localhost:8080/search?q={request.question}&page=1&limit=5")
        cpp_response.raise_for_status()
        search_data = cpp_response.json()

        snippets = [res.get("snippet", "") for res in search_data.get("results", [])]
        context = "\n---\n".join(snippets) if snippets else "No relevant context found."

        template = "Context: {context}\n\nQuestion: {question}\n\nAnswer based ONLY on the context:"
        prompt = PromptTemplate(template=template, input_variables=["context", "question"])
        
        chain = prompt | llm
        answer = chain.invoke({"context": context, "question": request.question})

        semantic_cache.append({"vector": question_vector, "answer": answer.content})
        return {"answer": answer.content, "cached": False}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# 6. OLLAMA PROXY ENDPOINT FOR C++ ENGINE
@app.post("/api/embeddings")
async def proxy_embeddings(request: Request):
    data = await request.json()
    text = data.get("prompt", "")
    vector = await embedder.aembed_query(text)
    return {"embedding": vector}

# 7. SERVE THE FRONTEND
# Note: Ensure the path points to your frontend folder correctly relative to main.py
app.mount("/", StaticFiles(directory="../frontend", html=True), name="frontend")

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 3000))
    uvicorn.run(app, host="0.0.0.0", port=port)