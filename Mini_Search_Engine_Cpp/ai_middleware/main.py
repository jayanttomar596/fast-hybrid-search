import os
from pathlib import Path
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

app = FastAPI(title="RAG Orchestration API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

CPP_BACKEND_URL = os.environ.get("CPP_BACKEND_URL", "http://127.0.0.1:8080").rstrip("/")
FRONTEND_DIR = Path(__file__).resolve().parent.parent / "frontend"

embedder = GoogleGenerativeAIEmbeddings(model="models/text-embedding-004")
llm = ChatGoogleGenerativeAI(model="gemini-1.5-flash", temperature=0)

MAX_CACHE_SIZE = 100
semantic_cache = deque(maxlen=MAX_CACHE_SIZE)
SIMILARITY_THRESHOLD = 0.95

class ChatRequest(BaseModel):
    question: str

# 1. CHAT ENDPOINT (Explicitly defined)
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
        cpp_response = requests.get(f"{CPP_BACKEND_URL}/search?q={request.question}&page=1&limit=5")
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

# 2. EMBEDDINGS ENDPOINT (Explicitly defined)
@app.post("/api/embeddings")
async def proxy_embeddings(request: Request):
    data = await request.json()
    text = data.get("prompt", "")
    vector = await embedder.aembed_query(text)
    return {"embedding": vector}

# 3. TRAFFIC COP: PROXY ALL OTHER /api/ REQUESTS TO C++
# Note: We put this last so it only catches requests that aren't /api/chat or /api/embeddings
@app.api_route("/api/{path:path}", methods=["GET", "POST", "OPTIONS"])
async def proxy_to_cpp(path: str, request: Request):
    params = dict(request.query_params)
    body = await request.body()
    headers = dict(request.headers)
    headers.pop("host", None) 
    
    try:
        cpp_url = f"{CPP_BACKEND_URL}/{path}"
        resp = requests.request(
            method=request.method,
            url=cpp_url,
            params=params,
            data=body,
            headers=headers,
            timeout=10
        )
        return Response(content=resp.content, status_code=resp.status_code, headers=dict(resp.headers))
    except Exception as e:
        print(f"DEBUG: C++ Engine failed: {str(e)}", flush=True)
        raise HTTPException(status_code=500, detail=f"C++ Engine unreachable: {str(e)}")

# 4. SERVE THE FRONTEND
app.mount("/", StaticFiles(directory=str(FRONTEND_DIR), html=True), name="frontend")

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 3000))
    uvicorn.run(app, host="0.0.0.0", port=port)