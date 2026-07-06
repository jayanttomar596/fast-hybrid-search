#include "SearchEngine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>
#include <algorithm>
#include <queue>
#include <filesystem>
#include <unordered_set>
#include <chrono>
#include <cstdlib>
#define CPPHTTPLIB_OPENSSL_SUPPORT // UST be defined before httplib.h
#include "httplib.h"
#include "json.hpp" // nlohmann/json
#include <fcntl.h>      // For file control (open)
#include <sys/mman.h>   // For memory mapping (mmap)
#include <sys/stat.h>   // For file size (fstat)
#include <unistd.h>     // For close()

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std;

static string resolveAppPath(const string& relativePath) {
    const char* envRoot = std::getenv("APP_ROOT");
    string root = envRoot && *envRoot ? envRoot : "/app";
    if (!root.empty() && root.back() == '/') root.pop_back();
    if (relativePath.empty()) return root;
    return root + "/" + relativePath;
}






int SearchEngine::getDocumentCount() const {
    return documents.size();
}

int SearchEngine::getVocabularySize() const {
    return invertedIndex.size();
}



// ---------------- COSINE SIMILARITY ----------------
double SearchEngine::cosineSimilarity(const vector<float>& A, const vector<float>& B) {
    if (A.empty() || B.empty() || A.size() != B.size()) return 0.0;
    
    double dotProduct = 0.0, normA = 0.0, normB = 0.0;
    for (size_t i = 0; i < A.size(); ++i) {
        dotProduct += A[i] * B[i];
        normA += A[i] * A[i];
        normB += B[i] * B[i];
    }
    
    if (normA == 0.0 || normB == 0.0) return 0.0;
    return dotProduct / (sqrt(normA) * sqrt(normB));
}

// ---------------- OLLAMA LOCAL API CALL ----------------
vector<float> SearchEngine::getOpenAIEmbedding(const string& text) {
    const char* env_url = std::getenv("EMBEDDING_SERVICE_URL");
    string embeddingServiceUrl = env_url ? env_url : "http://python-rag-gateway:3000";

    httplib::Client cli(embeddingServiceUrl.c_str());

    json req_body = {
        {"model", "nomic-embed-text"},
        {"prompt", text}
    };

    auto res = cli.Post("/api/embeddings", req_body.dump(), "application/json");

    if (res && res->status == 200) {
        json res_json = json::parse(res->body);
        return res_json["embedding"].get<vector<float>>();
    }

    cout << "Failed to fetch embedding from embedding service at " << embeddingServiceUrl << "\n";
    return {};
}


// ---------------- EDIT DISTANCE (LEVENSHTEIN) ----------------
int editDistance(const string& a, const string& b) {

    int n = a.size();
    int m = b.size();

    vector<vector<int>> dp(n + 1, vector<int>(m + 1));

    for (int i = 0; i <= n; i++)
        dp[i][0] = i;

    for (int j = 0; j <= m; j++)
        dp[0][j] = j;

    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {

            if (a[i - 1] == b[j - 1])
                dp[i][j] = dp[i - 1][j - 1];
            else {
                dp[i][j] = 1 + min({
                    dp[i - 1][j],     // delete
                    dp[i][j - 1],     // insert
                    dp[i - 1][j - 1]  // replace
                });
            }
        }
    }

    return dp[n][m];
}






// ---------------- SPELL CORRECTION ----------------
string correctWord(
    const string& queryWord,
    const unordered_map<string,
    unordered_map<int, Posting>>& index
){
    string bestWord = queryWord;
    int bestDist = INT_MAX;
    int bestDF = -1;

    for(const auto& [word, postingMap] : index){

        int dist = editDistance(queryWord, word);

        if(dist < bestDist){
            bestDist = dist;
            bestWord = word;
            bestDF = postingMap.size();
        }
        else if(dist == bestDist){
            int df = postingMap.size();

            if(df > bestDF){
                bestWord = word;
                bestDF = df;
            }
        }
    }

    return bestWord;
}






// ---------------- CACHE INVALIDATION ----------------
void SearchEngine::invalidateCache() {
    lock_guard<mutex> lock(cacheMutex);
    cacheMap.clear();
    lruList.clear();
}




// ---------------- NORMALIZE ----------------

// ------- OLD Normalize Function
/*
static string normalize(const string& word) {
    string clean;
    for (char c : word)
        if (isalnum(static_cast<unsigned char>(c))) // isalpha -> isalnum
            clean += tolower(c);
    return clean;
}
*/

// ------- NEW Normalize Function
static string normalize(const string& word) {

    string clean;

    auto isAllowedSpecial = [](char c) {
        return c == '+' || c == '#' || c == '.' ||
               c == '-' || c == '_' || c == ':';
    };

    for (size_t i = 0; i < word.size(); i++) {

        char c = word[i];

        if (isalnum(static_cast<unsigned char>(c))) {
            clean += tolower(c);
        }
        else if (isAllowedSpecial(c)) {

            if (!clean.empty()) {
                clean += c;
            }
        }
    }

    // Remove trailing specials
    while (!clean.empty() &&
           !isalnum(static_cast<unsigned char>(clean.back()))) {
        clean.pop_back();
    }

    if (!clean.empty() &&
        !isalnum(static_cast<unsigned char>(clean.front()))) {
        clean.clear();
    }

    return clean;
}





// ---------------- SPLIT QUERY ----------------
static vector<string> splitQuery(const string& query) {
    vector<string> tokens;
    string word;
    stringstream ss(query);

    while (ss >> word) {
        word = normalize(word);
        if (!word.empty())
            tokens.push_back(word);
    }

    return tokens;
}

// ---------------- PHRASE MATCH ----------------
static int countPhraseOccurrences(
    const vector<int>& pos1,
    const vector<int>& pos2,
    vector<int>& phrasePositions
) {
    int i = 0, j = 0, count = 0;

    while (i < pos1.size() && j < pos2.size()) {

        if (pos2[j] == pos1[i] + 1) {
            count++;
            phrasePositions.push_back(pos1[i]);
            i++; j++;
        }
        else if (pos2[j] > pos1[i] + 1)
            i++;
        else
            j++;
    }

    return count;
}



static int countProximityMatches(
    const vector<int>& pos1,
    const vector<int>& pos2,
    int k,
    vector<int>& proxPositions
) {

    int i = 0, j = 0, count = 0;

    while (i < pos1.size() && j < pos2.size()) {

        int gap = abs(pos2[j] - pos1[i]) - 1;

        if (gap <= k && gap >= 0) {
            count++;
            proxPositions.push_back(pos1[i]);
            i++; j++;
        }
        else if (pos1[i] < pos2[j])
            i++;
        else
            j++;
    }

    return count;
}




// ---------------- ADD DOCUMENT PATH ----------------
void SearchEngine::addDocument(const string& path) {
    documents.push_back(path);
}


// ---------------- ADD DOCUMENT CONTENT ----------------
/*
void SearchEngine::addDocumentContent(const string& name, const string& content) {

    if (usingSample)
        clearIndex();

    documents.push_back(name);
    int docID = documents.size() - 1;

    documentContents[docID] = content;

    indexDocument(docID, content);

    

    // buildIndex();
}
*/

void SearchEngine::addDocumentContent(const string& name, const string& content) {
    invalidateCache();

    string finalName = name;

    // 🔹 Handle duplicate filenames
    auto nameExists = [&](const string& checkName) {
        return find(documents.begin(), documents.end(), checkName) != documents.end();
    };

    if (nameExists(finalName)) {

        size_t dotPos = name.find_last_of('.');
        string base = (dotPos == string::npos) ? name : name.substr(0, dotPos);
        string ext  = (dotPos == string::npos) ? ""   : name.substr(dotPos);

        int counter = 1;
        while (nameExists(finalName)) {
            finalName = base + "(" + to_string(counter++) + ")" + ext;
        }
    }

    documents.push_back(finalName);
    int docID = documents.size() - 1;

    documentContents[docID] = content;

    // 🔹 Track vocabulary size before indexing
    size_t oldVocabSize = invertedIndex.size();

    // Incremental indexing
    indexDocument(docID, content);

    // 🔹 Update average document length
    double total = 0;
    for (auto& p : documentLength)
        total += p.second;

    if (!documentLength.empty())
        avgDocLength = total / documentLength.size();

    // 🔹 Insert only new words into Trie
    if (invertedIndex.size() > oldVocabSize) {
        for (auto& [word, postingMap] : invertedIndex) {
            trie.insert(word);
        }
    }
}



// ---------------- BUILD INDEX ----------------
/*
void SearchEngine::buildIndex() {

    for (int docID = 0; docID < documents.size(); docID++) {

        ifstream file(documents[docID]);
        if (!file) continue;

        stringstream buffer;
        buffer << file.rdbuf();

        documentContents[docID] = buffer.str();
        indexDocument(docID, buffer.str());
    }
}
*/


// ---- New BuildIndex for Threading 
void SearchEngine::buildIndex() {
    // just to check whether this function is called or not 
    // std::cout << "buildIndex() called\n";
    invalidateCache();

    auto start = std::chrono::high_resolution_clock::now(); // To track time

    invertedIndex.clear();
    documentLength.clear();
    documentContents.clear();
    avgDocLength = 0.0;
    trie = Trie();


    int totalDocs = documents.size();
    if (totalDocs == 0) return;

    // Decide number of threads
    unsigned int numThreads = thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    numThreads = min(numThreads, (unsigned int)totalDocs);

    int chunkSize = (totalDocs + numThreads - 1) / numThreads;

    vector<thread> threads;

    // Per-thread local structures
    vector<unordered_map<string, unordered_map<int, Posting>>> localIndexes(numThreads);
    vector<unordered_map<int, int>> localDocLengths(numThreads);
    vector<unordered_map<int, string>> localContents(numThreads); // New

    for (unsigned int t = 0; t < numThreads; t++) {

        int start = t * chunkSize;
        int end = min(start + chunkSize, totalDocs);

        if (start >= totalDocs) break;

        threads.emplace_back([&, t, start, end]() {
            // To check threads are working or not you may comment if you dont like it 
            cout << "Thread Index: " << t
            << " | System Thread ID: "
            << this_thread::get_id()
            << " | Processing Docs: "
            << start << " to " << end - 1
            << endl;

            for (int docID = start; docID < end; docID++) {

                ifstream file(documents[docID]);
                if (!file) continue;

                stringstream buffer;
                buffer << file.rdbuf();

                string content = buffer.str();

                // Safe: each docID handled by exactly one thread
                // documentContents[docID] = content;
                localContents[t][docID] = content;

                indexDocumentLocal(
                    docID,
                    content,
                    localIndexes[t],
                    localDocLengths[t]
                );
            }
        });
    }

    // Wait for all threads
    for (auto& th : threads)
        th.join();

    // ---------------- MERGE PHASE ----------------
    for (unsigned int t = 0; t < threads.size(); t++) {

        // 🔥 Merge document contents first
        for (auto& [docID, content] : localContents[t]) {
            documentContents[docID] = content;
        }

        for (auto& [word, postingMap] : localIndexes[t]) {

            auto& globalPostingMap = invertedIndex[word];

            for (auto& [docID, posting] : postingMap) {
                globalPostingMap[docID] = posting;
            }
        }

        for (auto& [docID, length] : localDocLengths[t]) {
            documentLength[docID] = length;
        }
    }

    // Recompute average document length
    double totalLength = 0;
    for (auto& [docID, length] : documentLength)
        totalLength += length;

    avgDocLength = totalLength / documentLength.size();

    // Rebuild Trie after merge
    trie = Trie();
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
    
    auto end = std::chrono::high_resolution_clock::now();

    lastIndexingTimeMs =
        std::chrono::duration<double, std::milli>(end - start).count();

    lastThreadCount = threads.size();

    cout << "Index built in "
        << lastIndexingTimeMs
        << " ms using "
        << lastThreadCount
        << " threads."
        << endl;

}







// ---------------- INDEX DOCUMENT ----------------
void SearchEngine::indexDocument(int docID, const string& content) {
    stringstream ss(content);
    string word;
    int position = 0;
    long long offset = 0;

    while (ss >> word) {
        string clean = normalize(word);
        if (clean.empty()) continue;

        auto& posting = invertedIndex[clean][docID];
        posting.frequency++;
        posting.positions.push_back(position);
        posting.offsets.push_back(offset);
        trie.insert(clean);

        offset += word.length() + 1;
        position++;
    }

    documentLength[docID] = position;
    
    // Efficiently update average by calculating the delta
    double total = 0;
    for(auto &p : documentLength)
        total += p.second;
    
    avgDocLength = total / documentLength.size();
}






// Local Indexing Function for Multithreading 

void SearchEngine::indexDocumentLocal(
    int docID,
    const string& content,
    unordered_map<string, unordered_map<int, Posting>>& localIndex,
    unordered_map<int, int>& localDocLength
) {
    stringstream ss(content);
    string word;
    int position = 0;
    long long offset = 0;

    while (ss >> word) {

        string clean = normalize(word);
        if (clean.empty()) continue;

        auto& posting = localIndex[clean][docID];
        posting.frequency++;
        posting.positions.push_back(position);
        posting.offsets.push_back(offset);

        offset += word.length() + 1;
        position++;
    }

    localDocLength[docID] = position;
}







double computeBM25(
    int tf,
    int df,
    int docLen,
    int N,
    double avgdl
){
    double k1 = 1.5;
    double b  = 0.75;

    double idf = log(1 + ((N - df + 0.5) / (df + 0.5)));
    

    double norm = (double)docLen / (double)avgdl;


    double num = tf * (k1 + 1.0);
    double den = tf + k1 *
        (1.0 - b + b * norm);

    return idf * (num / den);
}






// ======================= SEARCH API =======================
vector<SearchResult> SearchEngine::searchAPI(const string& query, int page, int limit) {

    // Cache key must combine query, page, and limit
    string cacheKey = query + "_p" + to_string(page) + "_l" + to_string(limit);

    
    {
        lock_guard<mutex> lock(cacheMutex); 
        if (cacheMap.find(cacheKey) != cacheMap.end()) {
            lruList.erase(cacheMap[cacheKey].second);
            lruList.push_front(cacheKey);
            cacheMap[cacheKey].second = lruList.begin();
            return cacheMap[cacheKey].first; 
        }
    }

    vector<SearchResult> results;
    vector<string> terms = splitQuery(query);


    string suggestedWord = "";

    for (string& term : terms) {
        if (invertedIndex.find(term) == invertedIndex.end()) {

            string corrected = correctWord(term, invertedIndex);

            if(corrected != term)
                suggestedWord = corrected;

            term = corrected;
        }
    }



    if (terms.empty()) return results;

    unordered_map<int, int> docPresence;

    for (const string& term : terms) {

        if (invertedIndex.find(term) == invertedIndex.end())
            return {};

        for (auto& [docID, posting] : invertedIndex[term]) {
            docPresence[docID]++;
        }
    }

    vector<int> candidateDocs;

    // 🔥 NEW: Fetch the vector for the user's search query
    vector<float> queryVector = getOpenAIEmbedding(query);

    // 🔥 NEW: Instead of candidateDocs, we score ALL documents in the corpus
    // so we can find semantic matches even if there are 0 exact word matches!
    vector<int> allDocs;
    for (int i = 0; i < documents.size(); i++) {
        allDocs.push_back(i);
    }

    auto cmp = [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score; 
    };
    priority_queue<SearchResult, vector<SearchResult>, decltype(cmp)> minHeap(cmp);
    int maxHeapSize = page * limit;

    // -------- RESULT GENERATION --------
    for (int docID : allDocs) {

        SearchResult res;
        res.document = documents[docID];
        res.suggestion = suggestedWord;
        string& content = documentContents[docID];

        // 1. Calculate BM25 (Lexical Score) exactly as before
        double bm25Score = 0.0;
        int N = documents.size();

        for(const string& term : terms) {
            if(invertedIndex[term].find(docID) != invertedIndex[term].end()) {
                auto& posting = invertedIndex[term].at(docID);
                bm25Score += computeBM25(posting.frequency, invertedIndex[term].size(), documentLength[docID], N, avgDocLength);
            }
        }

        // [Keep your existing Phrase & Proximity boosts here if desired, 
        // just ensure you check if terms exist in the doc first]

        // 2. NEW: Calculate Semantic Score (Vector Math)
        double semanticScore = 0.0;
        if (!queryVector.empty() && documentEmbeddings.find(docID) != documentEmbeddings.end()) {
            semanticScore = cosineSimilarity(queryVector, documentEmbeddings[docID]);
        }

        // 3. NEW: HYBRID COMBINATION
        // BM25 usually ranges from 0 to 20+, Cosine Similarity is 0 to 1.
        // We multiply the semantic score by 10 to give it weight alongside BM25.
        res.score = bm25Score + (semanticScore * 10.0);

        // Skip if score is 0 (no keyword match AND no semantic match)
        if (res.score <= 0.0) continue;

        // 4.  NEW: Safe Snippet Generation (Accounts for pure semantic matches)
        if (!terms.empty() && invertedIndex.find(terms[0]) != invertedIndex.end() && invertedIndex[terms[0]].find(docID) != invertedIndex[terms[0]].end()) 
        {
            auto& posting = invertedIndex[terms[0]][docID];
            res.frequency = posting.frequency;
            if (!posting.positions.empty()) {
                long long offset = posting.offsets[0];
                int start = max(0LL, offset - 60);
                int end = min((long long)content.size(), offset + 100);
                res.snippet = content.substr(start, end - start);
            }
        } 
        else 
        {
            // Semantic match fallback snippet (shows the beginning of the document)
            res.frequency = 0;
            res.snippet = content.substr(0, min((int)content.size(), 150)) + "...";
        }

        minHeap.push(res);
        if (minHeap.size() > maxHeapSize) {
            minHeap.pop(); 
        }
    }

    // ... [Keep your existing heap extraction and LRU caching logic here exactly as before] ...

    // NEW: Clear initial vector and extract target page
    results.clear(); 
    int startIndex = (page - 1) * limit;

    if (minHeap.size() > startIndex) {
        vector<SearchResult> tempResults;
        while (!minHeap.empty()) {
            tempResults.push_back(minHeap.top());
            minHeap.pop();
        }
        
        // Reverse to get highest scores first
        reverse(tempResults.begin(), tempResults.end());

        int endIndex = min((int)tempResults.size(), startIndex + limit);
        for (int i = startIndex; i < endIndex; i++) {
            results.push_back(tempResults[i]);
        }
    }

    // 2. STORE RESULT IN CACHE BEFORE RETURNING
    {
        lock_guard<mutex> lock(cacheMutex); 
        
        // Evict the oldest query if we hit capacity
        if (cacheMap.size() >= cacheCapacity) {
            string last = lruList.back();
            lruList.pop_back();
            cacheMap.erase(last);
        }
        
        // Insert new query at the front
        lruList.push_front(cacheKey);
        cacheMap[cacheKey] = {results, lruList.begin()};
    }

    return results;
}



// ---------------- AUTOCOMPLETE ----------------
vector<string> SearchEngine::autocompleteAPI(const string& prefix) {
    return trie.autocomplete(normalize(prefix));
}

// ---------------- CLEAR INDEX ----------------
void SearchEngine::clearIndex() {
    invalidateCache();

    documents.clear();
    invertedIndex.clear();
    documentContents.clear();
    documentLength.clear();   // MISSING BEFORE
    avgDocLength = 0.0;       // RESET THIS TOO

    trie = Trie();

    usingSample = false;
    includeInitialCorpus = false; // New added for check
}



// ---------------- LOAD SAMPLE ----------------

void SearchEngine::loadSampleDataset() {
    invalidateCache();

    clearIndex();   // 🔥 Always reset

    namespace fs = std::filesystem;

    string folderPath = resolveAppPath("documents");

    for (const auto& entry : fs::directory_iterator(folderPath)) {

        if (entry.is_regular_file() &&
            entry.path().extension() == ".txt") {

            documents.push_back(entry.path().string());
        }
    }

    if (documents.empty()) {
        cout << "No .txt files found in documents folder\n";
        return;
    }

    buildIndex();  // multithreaded

    includeInitialCorpus = true; // new addition for check 
}



double SearchEngine::getLastIndexingTime() const {
    return lastIndexingTimeMs;
}

int SearchEngine::getLastThreadCount() const {
    return lastThreadCount;
}




void SearchEngine::buildIndexSingleThread() {
    invalidateCache();

    invertedIndex.clear();
    documentLength.clear();
    documentContents.clear();
    avgDocLength = 0.0;
    trie = Trie();

    for (int docID = 0; docID < documents.size(); docID++) {

        ifstream file(documents[docID]);
        if (!file) continue;

        stringstream buffer;
        buffer << file.rdbuf();

        string content = buffer.str();
        documentContents[docID] = content;

        indexDocument(docID, content);
    }

    // Recompute average doc length
    double totalLength = 0;
    for (auto& [docID, length] : documentLength)
        totalLength += length;

    if (!documentLength.empty())
        avgDocLength = totalLength / documentLength.size();

    // Build Trie
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
}





void SearchEngine::scanCorpusFolders() {

    namespace fs = std::filesystem;

    documents.clear();

    // Include permanent corpus ONLY if enabled
    if (includeInitialCorpus) {

        for (const auto& entry : fs::directory_iterator(resolveAppPath("documents"))) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                documents.push_back(entry.path().string());
            }
        }
    }

    // Always include runtime corpus
    for (const auto& entry : fs::directory_iterator(resolveAppPath("runtime_corpus"))) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".txt") {
            documents.push_back(entry.path().string());
        }
    }
}






void SearchEngine::indexSingleDocument(const string& path) {
    invalidateCache();

    int docID = documents.size();
    documents.push_back(path);

    ifstream file(path);
    if (!file) return;

    stringstream buffer;
    buffer << file.rdbuf();
    string content = buffer.str();

    documentContents[docID] = content;

    cout << "Fetching OpenAI Vector for: " << path << "...\n";
    documentEmbeddings[docID] = getOpenAIEmbedding(content);

    indexDocument(docID, content);

    // Update average document length
    double total = 0;
    for (auto& p : documentLength)
        total += p.second;

    if (!documentLength.empty())
        avgDocLength = total / documentLength.size();

    // Insert words into Trie
    for (auto& [word, _] : invertedIndex)
        trie.insert(word);
}









// ---------------- SAVE INDEX TO DISK (BINARY) ----------------
void SearchEngine::saveIndex(const string& filepath) {
    lock_guard<mutex> lock(cacheMutex); // Lock to ensure no reads happen while saving
    
    ofstream out(filepath, ios::binary);
    if (!out) {
        cout << "Failed to open file for saving: " << filepath << endl;
        return;
    }

    // 1. Save average document length
    out.write((char*)&avgDocLength, sizeof(avgDocLength));

    // 2. Save Documents Array
    size_t docCount = documents.size();
    out.write((char*)&docCount, sizeof(docCount));
    for (const string& doc : documents) {
        size_t len = doc.size();
        out.write((char*)&len, sizeof(len));
        out.write(doc.c_str(), len);
    }

    // 3. Save Document Lengths
    size_t dlSize = documentLength.size();
    out.write((char*)&dlSize, sizeof(dlSize));
    for (const auto& [docID, len] : documentLength) {
        out.write((char*)&docID, sizeof(docID));
        out.write((char*)&len, sizeof(len));
    }

    // 4. Save Inverted Index
    size_t vocabSize = invertedIndex.size();
    out.write((char*)&vocabSize, sizeof(vocabSize));
    for (const auto& [word, postingMap] : invertedIndex) {
        // Write word
        size_t wordLen = word.size();
        out.write((char*)&wordLen, sizeof(wordLen));
        out.write(word.c_str(), wordLen);

        // Write posting map
        size_t mapSize = postingMap.size();
        out.write((char*)&mapSize, sizeof(mapSize));
        for (const auto& [docID, posting] : postingMap) {
            out.write((char*)&docID, sizeof(docID));
            out.write((char*)&posting.frequency, sizeof(posting.frequency));
            
            size_t posSize = posting.positions.size();
            out.write((char*)&posSize, sizeof(posSize));
            out.write((char*)posting.positions.data(), posSize * sizeof(int));

            size_t offSize = posting.offsets.size();
            out.write((char*)&offSize, sizeof(offSize));
            out.write((char*)posting.offsets.data(), offSize * sizeof(long long));
        }
    }

    out.close();
    cout << "Index successfully saved to " << filepath << endl;
}








// ---------------- LOAD INDEX FROM DISK (MMAP) ----------------
bool SearchEngine::loadIndex(const string& filepath) {
    invalidateCache();

    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd < 0) return false;

    // Get exact file size
    struct stat sb;
    if (fstat(fd, &sb) < 0) { close(fd); return false; }

    if (sb.st_size == 0) { close(fd); return false; }

    // 🔥 MEMORY MAP THE FILE DIRECTLY TO RAM
    char* map = (char*)mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return false; }

    char* ptr = map; // Pointer to traverse the mapped memory

    clearIndex();

    // 1. Read avgDocLength
    avgDocLength = *(double*)ptr; ptr += sizeof(double);

    // 2. Read Documents Array
    size_t docCount = *(size_t*)ptr; ptr += sizeof(size_t);
    for (size_t i = 0; i < docCount; i++) {
        size_t len = *(size_t*)ptr; ptr += sizeof(size_t);
        documents.push_back(string(ptr, len));
        ptr += len;
    }

    // 3. Read Document Lengths
    size_t dlSize = *(size_t*)ptr; ptr += sizeof(size_t);
    for (size_t i = 0; i < dlSize; i++) {
        int docID = *(int*)ptr; ptr += sizeof(int);
        int len = *(int*)ptr; ptr += sizeof(int);
        documentLength[docID] = len;
    }

    // 4. Read Inverted Index
    size_t vocabSize = *(size_t*)ptr; ptr += sizeof(size_t);
    for (size_t i = 0; i < vocabSize; i++) {
        size_t wordLen = *(size_t*)ptr; ptr += sizeof(size_t);
        string word(ptr, wordLen);
        ptr += wordLen;

        trie.insert(word); // Rebuild Trie on the fly

        size_t mapSize = *(size_t*)ptr; ptr += sizeof(size_t);
        for (size_t j = 0; j < mapSize; j++) {
            int docID = *(int*)ptr; ptr += sizeof(int);
            
            Posting& posting = invertedIndex[word][docID];
            posting.frequency = *(int*)ptr; ptr += sizeof(int);

            size_t posSize = *(size_t*)ptr; ptr += sizeof(size_t);
            posting.positions.resize(posSize);
            memcpy(posting.positions.data(), ptr, posSize * sizeof(int));
            ptr += posSize * sizeof(int);

            size_t offSize = *(size_t*)ptr; ptr += sizeof(size_t);
            posting.offsets.resize(offSize);
            memcpy(posting.offsets.data(), ptr, offSize * sizeof(long long));
            ptr += offSize * sizeof(long long);
        }
    }

    // Clean up memory mapping
    munmap(map, sb.st_size);
    close(fd);

    cout << "Index successfully loaded via mmap from " << filepath << endl;
    return true;
}





// ---------------- GARBAGE COLLECTION ----------------
void SearchEngine::cleanupOrphanFiles() {
    namespace fs = std::filesystem;
    
    // 1. Put all valid, saved document paths into a fast lookup set
    unordered_set<string> validFiles(documents.begin(), documents.end());

    // 2. Scan the runtime_corpus folder
    for (const auto& entry : fs::directory_iterator(resolveAppPath("runtime_corpus"))) {
        if (entry.is_regular_file()) {
            string filePath = entry.path().string();
            
            // 3. If the physical file is NOT in our saved index, delete it!
            if (validFiles.find(filePath) == validFiles.end()) {
                cout << "Deleting unsaved orphan file: " << filePath << endl;
                fs::remove(entry.path());
            }
        }
    }
}


