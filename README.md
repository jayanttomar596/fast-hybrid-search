# Fast Hybrid Search Engine in C++

### Multithreaded Inverted Index • BM25 Ranking • Trie Autocomplete • REST APIs • Web UI

This project implements a **dynamic full-stack search engine written in C++** that demonstrates several core ideas used in real-world search systems.

It supports **fast document retrieval using an inverted index**, **BM25 ranking**, **phrase & proximity boosting**, **Trie-based autocomplete**, **hybrid semantic search with AI embeddings**, **LRU result caching**, **PDF ingestion**, **persistent indexing with mmap**, and **runtime document ingestion** with an interactive web interface.

The system is designed with **clean architecture, performance-aware indexing, and modular components**, making it a great demonstration of **information retrieval, data structures, concurrency, backend system design, and production-grade engineering practices**.

---

# Demo

## Working Video
https://youtu.be/JqCNUgTDCAs

---

# Project Highlights

## Fast Ranked Search
- Uses **inverted index** for near O(1) term lookup
- Implements **BM25 ranking algorithm**
- Supports **multi-word queries**

## Enterprise RAG Search Engine

A high-performance, containerized Retrieval-Augmented Generation (RAG) system built with a decoupled microservices architecture. 

This project bridges the gap between low-level systems programming and modern AI orchestration. It features a custom-built C++ search engine for high-speed lexical retrieval, paired with a Python FastAPI gateway for context-aware LLM generation and semantic caching.

## Architecture
The system is divided into three isolated Docker containers:
1. **The Data Engine (C++):** A custom search backend featuring an Inverted Index, BM25 scoring, a Trie for autocomplete, and POSIX `mmap` for zero-copy file reads.
2. **The AI Gateway (Python/FastAPI):** A middleware orchestrator that handles cross-origin requests, constructs LangChain prompts, and manages an LRU Semantic Cache.
3. **The LLM (Ollama):** Hosts `llama3` for natural language generation and `nomic-embed-text` for vector math.

## Key Features
* **Microservices Design:** Total separation of concerns between data retrieval (Port 8080) and AI inference (Port 3000).
* **Semantic Caching:** Implements an LRU bounding cache (`collections.deque`) using cosine similarity on vector embeddings to bypass redundant LLM calls, drastically reducing latency.
* **Memory Safety:** Optimized C++ destructors and $O(1)$ length calculations prevent memory leaks during high-throughput indexing.
* **Infrastructure as Code:** Fully containerized via `docker-compose` for guaranteed cross-platform reproducibility.

## Quick Start (One-Click Deploy)
Ensure you have Docker Desktop installed, then run:
```bash
docker compose up --build
```
Frontend: http://localhost:8080/ (or open index.html)
AI API Gateway: http://localhost:3000/docs



## Intelligent Ranking Signals
Search relevance is improved using:
- **BM25 scoring**
- **Phrase boosting**
- **Proximity boosting (k-word window)**

## Spell Correction
- Implemented using **Levenshtein edit distance**
- Uses **document frequency tie-breaking** to choose best correction

Example:


recieve → receive
serach → search


## Trie-Based Autocomplete
- Prefix suggestions using **Trie**
- Supports extended tokens such as:


C++
file.txt
snake_case
namespace::function


## Multithreaded Indexing
Initial corpus indexing uses **parallel processing**:


Thread 1 → Documents 0–N
Thread 2 → Documents N–2N
Thread 3 → Documents 2N–3N


Each thread builds a **local inverted index**, followed by a **merge phase**.

This avoids:
- mutex locks
- race conditions
- lock contention

## Hybrid Indexing Pipeline

The system uses **two indexing strategies**:

| Workload | Strategy |
|--------|--------|
| Initial corpus | Multithreaded indexing |
| Runtime uploads | Incremental single-document indexing |

This ensures:
- fast bulk indexing
- instant availability of uploaded documents

## Runtime Corpus Management

Uploaded documents are stored in a **temporary runtime corpus**:


runtime_corpus/


Behavior:
- documents become searchable immediately
- folder automatically resets when the server restarts
- prevents stale runtime data accumulation

## Performance Benchmarking

The system includes a **benchmark endpoint** comparing:

- Single-thread indexing
- Multi-thread indexing
- Speedup ratio

Displayed directly in the UI.

## System Observability

The web interface displays:

- Search latency
- Indexing latency
- Corpus size
- Vocabulary size
- Threads used during indexing

---

# Recent Enhancements: Five-Phase Architecture Upgrade

The project has undergone significant production-grade enhancements across five distinct phases:

## Phase 1: Thread-Safe Query Result Cache (LRU)

**Goal:** Eliminate redundant BM25 calculations by caching and instantly returning results for repeated queries.

**Implementation:**
- Built a **Least Recently Used (LRU) cache** using `std::list` (tracking query recency) and `std::unordered_map` (linking queries to cached results)
- Implemented **thread-safe cache operations** using `std::mutex` and `std::lock_guard` with minimal lock scope
- Heavy BM25 scoring remains unlocked, allowing concurrent query processing via `cpp-httplib`
- **Automatic cache invalidation** when the corpus changes (new documents, index rebuild, corpus clear) ensures fresh results
- Cache keys incorporate pagination parameters to prevent conflicts between different result pages

**Impact:** Dramatically reduces CPU usage for repeated queries while maintaining thread safety across concurrent requests.

---

## Phase 2: Top-K Retrieval & Pagination

**Goal:** Prevent API overload and browser crashes when searching massive datasets by returning only requested document counts.

**Implementation:**
- Replaced expensive full array sorting with **Min-Heap** (`std::priority_queue`) to efficiently retain only Top-K results
- Added `page` and `limit` URL parameters to the `/search` endpoint for granular result control
- Integrated pagination keys into the LRU cache to ensure different pages cache independently
- Efficient heap operations maintain O(n log k) complexity instead of O(n log n)

**Impact:** Enables search over billion-document corpora without memory or latency penalties; practical pagination for large result sets.

---

## Phase 3: PDF Ingestion & CORS Preflight Handling

**Goal:** Expand the engine to natively parse and ingest binary `.pdf` files alongside `.txt` files.

**Implementation:**
- Updated file-saving logic to use `ios::binary` and `out.write()` to preserve null bytes and prevent data corruption
- Integrated system-level PDF text extraction using `pdftotext` (Poppler) via C++ system calls
- Converted PDFs to raw text before feeding into the inverted index pipeline
- Added **CORS Preflight (`OPTIONS`) handler** to accept `application/pdf` payloads from browsers

**Impact:** Users can now upload PDF documents directly; the engine automatically extracts text and makes content searchable.

---

## Phase 4: Hybrid Semantic Search (BM25 + AI Embeddings)

**Goal:** Transition from purely lexical (keyword) matching to hybrid search capable of finding conceptual and semantic matches.

**Implementation:**
- Integrated **Ollama (`nomic-embed-text`)** to run a local neural network converting documents and queries into high-dimensional vector embeddings
- Implemented **Cosine Similarity** in C++ to compute distance between query and document vectors
- Created a **Hybrid Scoring Formula** that mathematically combines:
  - Lexical Score: `BM25 + Phrase Boost + Proximity Boost`
  - Semantic Score: `Cosine Similarity of embeddings`
- Modified build system to include `nlohmann/json` parser and link **OpenSSL** (`-lssl -lcrypto`) for secure HTTP communication with Ollama

**Impact:** Users can now find documents by meaning, not just keywords. Queries like "fast algorithms" now match documents discussing "rapid computation" even without exact keyword overlap.

---

## Phase 5: Persistent Storage, `mmap`, and Garbage Collection

**Goal:** Enable zero-overhead server restarts and eliminate the need to rebuild the index on every boot.

**Implementation:**

**Binary Serialization:**
- Wrote `saveIndex` function that flattens the in-RAM `unordered_map` (inverted index, vocabulary, offsets, frequencies) into a contiguous binary file (`search_index.bin`)

**Memory Mapping (`mmap`):**
- Implemented `loadIndex` using POSIX `mmap` to treat the binary file on SSD as if it were in RAM
- OS fetches only the chunks needed for each query, eliminating full index load overhead
- Drops server startup time from minutes to near-zero

**Enterprise Folder Isolation:**
- Separated user-uploaded raw documents (`runtime_corpus/`) from internal binary indexes (`database/`)
- Prevents accidental data corruption or mixing of source documents with binary serialized data

**Startup Garbage Collection:**
- Built a startup routine that cross-references physical files with the loaded index
- Automatically detects and deletes "Orphan Files" (uploaded documents never indexed)
- Completely prevents Segmentation Faults during snippet extraction

**Build System Enhancements:**
- Customized `Makefile` compiles with modern **C++17** (`-std=c++17`), multi-threading (`-pthread`), and OpenSSL linking

**Impact:** Production-ready persistence; servers restart instantly; no data loss; automatic cleanup of orphaned files; efficient use of RAM and SSD storage.

---

## Performance Benchmarks

### Corpus Statistics

- Documents Indexed: 1004
- Vocabulary Size: 33,360 unique terms

### Indexing Performance

| Mode | Time |
|---|---|
| Single-threaded | 838.958 ms |
| Multi-threaded | 392.348 ms |
| Threads Used | 8 |
| Speedup | 2.14× |

### Search Performance

| Metric | Result |
|---|---|
| Average Latency | 0.758 ms |
| P95 Latency | 1.015 ms |
| Min Latency | 0.601 ms |
| Max Latency | 4.596 ms |
| Throughput | 1319.26 queries/sec |

### Benchmark Screenshots

![Index Benchmark](Images/benchmark.png)

![Search Benchmark](Images/avg_latency.png)


### Components

| Layer | Responsibility |
|------|---------------|
| Frontend | User interface & query visualization |
| HTTP Server | REST API layer |
| SearchEngine | Indexing, ranking, query processing |
| Trie | Autocomplete suggestions |
| Inverted Index | Word → document mapping |

---

## Directory Structure

```
Mini_Search_Engine_C++/
│
├── backend/                          # C++ Data Engine (Port 8080)
│   ├── include/
│   │   ├── SearchEngine.h            # Core search engine interface
│   │   ├── Trie.h                    # Autocomplete data structure
│   │   ├── httplib.h                 # HTTP server library (header-only)
│   │   └── json.hpp                  # JSON parsing library (header-only)
│   │
│   ├── src/
│   │   ├── SearchEngine.cpp          # Inverted index implementation, BM25 ranking
│   │   └── Trie.cpp                  # Trie-based autocomplete implementation
│   │
│   ├── server.cpp                    # Main REST API server
│   ├── Makefile                      # Build configuration
│   ├── server                        # Compiled binary (executable)
│   └── Dockerfile                    # Container definition for C++ backend
│
├── ai_middleware/                    # Python FastAPI Gateway (Port 3000)
│   ├── main.py                       # FastAPI application entry point
│   ├── requirements.txt               # Python dependencies (FastAPI, LangChain, etc.)
│   └── Dockerfile                    # Container definition for AI middleware
│
├── frontend/                         # Web UI Interface (Served from Backend)
│   ├── index.html                    # Main UI page
│   ├── script.js                     # Frontend logic & API interactions
│   └── style.css                     # UI styling
│
├── documents/                        # Sample Documents / Corpus
│   ├── doc1.txt                      # Test document
│   ├── doc2.txt                      # Test document
│   ├── doc3.txt                      # Test document
│   └── newfile.txt                   # Additional test document
│
├── docker-compose.yml                # Multi-container orchestration
├── RUNNING_MANNUAL.txt               # Deployment & usage instructions
```

---

## Component Architecture

### 1. **C++ Backend (Port 8080)**
**Location:** `backend/`

**Responsibilities:**
- Fast lexical search using inverted index
- BM25 ranking algorithm for relevance scoring
- Phrase & proximity boosting
- Trie-based autocomplete functionality
- POSIX `mmap` for zero-copy file reads
- Persistent indexing with memory efficiency
- REST API endpoints

**Key Data Structures:**
- **Inverted Index:** Maps terms to documents for O(1) term lookup
- **Trie:** Prefix-based autocomplete suggestions
- **BM25 Scorer:** Statistical ranking of search results
- **LRU Cache:** Result caching for frequently searched queries

**Core Files:**
- `SearchEngine.h/cpp` — Inverted index, BM25 scoring, document ingestion
- `Trie.h/cpp` — Autocomplete suggestions using Trie data structure
- `server.cpp` — HTTP REST API server implementation

**Build System:**
- `Makefile` — Compilation and linking configuration
- `server` — Compiled executable (output binary)

---

### 2. **Python FastAPI Gateway (Port 3000)**
**Location:** `ai_middleware/`

**Responsibilities:**
- Cross-Origin Resource Sharing (CORS) handling
- LangChain prompt construction & orchestration
- LLM inference coordination via Ollama
- Semantic caching using LRU with embeddings
- Context augmentation (RAG)
- Request/response transformation

**Key Features:**
- **Semantic Cache:** Uses cosine similarity on vector embeddings to bypass redundant LLM calls
- **LRU Bounded Cache:** Memory-efficient caching using `collections.deque`
- **Zero-Copy Architecture:** Direct proxying to C++ backend
- **Async/Await:** Non-blocking API calls for high throughput

**Core Files:**
- `main.py` — FastAPI application with routes, middleware, and cache logic
- `requirements.txt` — Python package dependencies

---

### 3. **Frontend Web UI**
**Location:** `frontend/`

**Responsibilities:**
- Interactive search interface
- Real-time autocomplete suggestions
- Display ranked search results
- Runtime document ingestion UI
- WebSocket support (optional)

**Core Files:**
- `index.html` — Main HTML interface
- `script.js` — Client-side logic, API communication, event handling
- `style.css` — Responsive UI styling

**Integration:**
- Communicates with AI Gateway (Port 3000) for semantic queries
- Fallback to Backend (Port 8080) for lexical queries
- Supports both BM25 and LLM-enhanced results

---

### 4. **Sample Documents/Corpus**
**Location:** `documents/`

**Purpose:**
- Test data for search indexing
- Benchmark and validation corpus

**Files:**
- `doc1.txt`, `doc2.txt`, `doc3.txt`, `newfile.txt` — Sample documents

---

## Microservices Communication Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    Web Browser (Client)                      │
└──────────────────────┬──────────────────────────────────────┘
                       │
        ┌──────────────┴──────────────┐
        │                             │
        │ (HTTP/REST)                 │ (HTTP/REST)
        ▼                             ▼
┌─────────────────────┐      ┌────────────────────┐
│   Frontend (HTML)   │      │  AI Gateway        │
│   Port 8080/web     │      │  Port 3000         │
└──────────┬──────────┘      └─────────┬──────────┘
           │                           │
           │ (Internal API)            │ (Internal API)
           └───────────────┬───────────┘
                           │
                    ┌──────┴──────┐
                    │             │
                    ▼             ▼
            ┌──────────────┐  ┌─────────────┐
            │ C++ Backend  │  │  Ollama LLM │
            │ Port 8080    │  │ (llama3)    │
            └──────┬───────┘  └─────────────┘
                   │
                   ▼
            ┌──────────────────────┐
            │  Inverted Index      │
            │  + BM25 + Trie Cache │
            │  + Document Corpus   │
            └──────────────────────┘
```

---

# Requirements & Setup

## System Requirements

- **C++17** or higher
- **POSIX-compliant OS** (Linux, macOS, BSD) for `mmap` support
- **OpenSSL libraries** (`libssl-dev` on Linux, or via Homebrew on macOS)
- **GNU Make** or compatible build system

## Optional Dependencies

### For Hybrid Semantic Search
- **Ollama** (https://ollama.ai) with the `nomic-embed-text` model
  - Install: `ollama pull nomic-embed-text`
  - Running on: `http://localhost:11434` (default)
  - Enables semantic search via AI embeddings (Phase 4)

### For PDF Ingestion
- **Poppler utilities** (`pdftotext`)
  - Linux: `sudo apt-get install poppler-utils`
  - macOS: `brew install poppler`
  - Enables PDF document upload and text extraction (Phase 3)

## Build & Compile

```bash
cd Mini_Search_Engine_C++/backend
make clean
make build
./server
```

The `Makefile` automatically handles C++17 compilation, pthread linking, and OpenSSL dependencies.

---

## System Architecture

```
                                          User (Browser)
                                              ↓
                                      Frontend (HTML / CSS / JS)
                                              ↓ HTTP Requests
                                     C++ HTTP Server (cpp-httplib)
                                              ↓
                                ┌─────────────────────────────────────┐
                                │  SearchEngine Core                   │
                                │  ┌─────────────────────────────────┐ │
                                │  │ Query Processing Pipeline       │ │
                                │  │  1. LRU Result Cache Check      │ │
                                │  │  2. Tokenization & Correction   │ │
                                │  │  3. Candidate Retrieval         │ │
                                │  │  4. Ranking (BM25 + Proximity)  │ │
                                │  │  5. Semantic Scoring (Optional) │ │
                                │  │  6. Hybrid Score Combination    │ │
                                │  │  7. Top-K Selection (Min-Heap)  │ │
                                │  │  8. Pagination & Snippet Ext.   │ │
                                │  └─────────────────────────────────┘ │
                                └─────────────────────────────────────┘
                                              ↓
                                ┌──────────────────────┬──────────────────────┐
                                │  Inverted Index      │  Memory-Mapped Index │
                                │  (RAM)               │  (SSD via mmap)      │
                                │                      │                      │
                                │ • Vocabulary         │ • Serialized Index   │
                                │ • Frequencies        │ • Offsets            │
                                │ • Positions          │ • Inverted Lists     │
                                │ • Byte Offsets       │                      │
                                └──────────────────────┴──────────────────────┘
                                              ↓
                                ┌───────────────┬─────────────────┬──────────────────┐
                                │  Trie         │  Cache Layer    │  Ollama (Remote) │
                                │ (Autocomplete)│  (LRU)          │  (Embeddings)    │
                                └───────────────┴─────────────────┴──────────────────┘
                                              ↓
                                ┌──────────────────┬──────────────────┬──────────────┐
                                │  Documents       │  Binary Database │  Orphan      │
                                │  (runtime_corpus)│  (database/)     │  Cleanup GC  │
                                └──────────────────┴──────────────────┴──────────────┘
```

**Data Flow:**
1. **Upload Phase:** Users upload `.txt` or `.pdf` files → PDF extraction (if applicable) → incremental indexing → cache invalidation
2. **Query Phase:** Query enters cache layer → cache hit returns instantly → cache miss proceeds through ranking pipeline → optional semantic scoring via Ollama → results cached
3. **Persistence Phase:** Index serialized to binary file on SSD → next startup loads via `mmap` → garbage collection removes orphaned files

---


## Indexing Phase

Documents are processed once during indexing.

For each word:
- normalize text
- update inverted index
- store frequency
- store word positions
- store byte offsets
- insert word into Trie

Example index entry:


search → {
doc1 : {freq=3, positions=[2,15,30]}
doc2 : {freq=1, positions=[7]}
}


---

## Query Processing Phase

When a user submits a query:


machine learning


Steps:

1. Tokenize and normalize query  
2. Spell-correct unknown terms  
3. Retrieve candidate documents  
4. Compute **BM25 ranking score**  
5. Apply phrase boost  
6. Apply proximity boost  
7. Extract contextual snippet  
8. Sort and return results  

---

# Ranking Formula

The engine uses **BM25 scoring**:


Score = Σ BM25(term, doc)
+ PhraseBoost
+ ProximityBoost


BM25 parameters:


k1 = 1.5
b = 0.75


Phrase boost:


1.5 × phrase_frequency


Proximity boost:


0.75 × proximity_matches


---

# REST API Endpoints

| Endpoint | Method | Parameters | Description |
|--------|--------|-----------|-------------|
| `/search` | `GET` | `q=`, `page=`, `limit=` | Search query with pagination support |
| `/autocomplete` | `GET` | `prefix=` | Prefix suggestions |
| `/upload` | `POST` | `file` (`.txt` or `.pdf`) | Upload new document |
| `/loadSample` | `GET` | — | Load initial corpus |
| `/rebuildIndex` | `GET` | — | Rebuild index from current corpus |
| `/corpusInfo` | `GET` | — | Corpus statistics |
| `/benchmark` | `GET` | — | Indexing benchmark comparison |
| `/clearCorpus` | `GET` | — | Clear runtime corpus |
| `/OPTIONS` | `OPTIONS` | — | CORS preflight handler for PDF uploads |

**Caching Behavior:**
- Search results are cached using LRU strategy with automatic invalidation on corpus changes
- Pagination parameters are included in cache keys to prevent conflicts
- Cache is thread-safe and optimized for concurrent queries

**Embedding Integration:**
- `/search` endpoint integrates Ollama embeddings (when available) for hybrid semantic + lexical search
- Requires `nomic-embed-text` model running locally in Ollama

---

# Example Queries

### Exact Search


search engine
machine learning
inverted index


### Autocomplete


se → search, search-engine
ma → machine, machine-learning


### Spell Correction


recieve → receive
serach → search


---

# Time Complexity

| Operation | Complexity |
|-----------|-----------|
| Indexing | O(total words) |
| Search | O(candidate_docs log candidate_docs) |
| Autocomplete | O(prefix_length + results) |
| Spell Correction | O(vocabulary × word_length²) |

---

# Concepts Used

**Core Information Retrieval:**
- Inverted Index
- BM25 Ranking
- Phrase & Proximity Search
- Trie Data Structure
- Edit Distance (Levenshtein)

**Advanced Features:**
- LRU Cache with Thread-Safe Operations
- Min-Heap for Top-K Retrieval
- Vector Embeddings & Cosine Similarity
- Hybrid Semantic + Lexical Scoring

**Systems & Performance:**
- Multithreaded Processing
- Memory Mapping (`mmap`)
- Binary Serialization
- Cache Invalidation
- Garbage Collection

**Engineering:**
- REST API Design
- CORS Preflight Handling
- PDF Parsing (via Poppler)
- File System Management
- Frontend–Backend Integration
- Production-Ready Error Handling

---

# Future Improvements

**Potential upgrades:**

- **Query Language Support:** Advanced query syntax (AND, OR, NOT operators; field-specific search)
- **Distributed Architecture:** Multi-machine indexing and search federation
- **Advanced Caching:** Temporal cache with predictive preloading based on query patterns
- **Query Analysis:** Real-time query performance analytics and optimization suggestions
- **Reranking Pipeline:** Learning-to-rank (LTR) models for personalized result reranking
- **Incremental Indexing:** Delta-based updates for massive corpus modifications
- **Sharding Strategy:** Automatic corpus partitioning for horizontal scalability
- **Hybrid Index Options:** BM25 + Dense Passage Retrieval (DPR) combinations

**Already Implemented (Recent Phases):**
- ✅ Persistent disk-based index with mmap
- ✅ Query result caching with LRU eviction
- ✅ PDF document ingestion
- ✅ Hybrid semantic + lexical search with embeddings
- ✅ Top-K retrieval with pagination
- ✅ Thread-safe concurrent query processing
- ✅ Automatic cache invalidation and garbage collection

---

# Author

**Jayant Tomar**

Computer Science Engineering — Delhi Technological University

Focus Areas:
- Information Retrieval
- Backend Systems
- Search Infrastructure
- Performance Optimization
