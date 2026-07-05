#include "httplib.h"
#include "SearchEngine.h"
#include <iostream>
#include <fstream>
#include <chrono>   
#include <cstdlib>
#include<filesystem>
namespace fs = std::filesystem;

using namespace std;   


string escapeJson(const string& s) {
    string out;
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}


// ---------------- JSON Helper ----------------
string toJson(const vector<SearchResult>& results) {
    string json = "{ \"results\": [";

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];

        json += "{";
        json += "\"document\":\"" + r.document + "\",";
        json += "\"suggestion\":\"" + r.suggestion + "\",";
        json += "\"frequency\":" + to_string(r.frequency) + ",";
        json += "\"score\":" + to_string(r.score) + ",";


        json += "\"positions\":[";
        for (size_t j = 0; j < r.positions.size(); j++) {
            json += to_string(r.positions[j]);
            if (j + 1 < r.positions.size()) json += ",";
        }
        json += "],";

        json += "\"offsets\":[";
        for (size_t j = 0; j < r.offsets.size(); j++) {
            json += to_string(r.offsets[j]);
            if (j + 1 < r.offsets.size()) json += ",";
        }
        json += "]";

        json += ",\"snippet\":\"" + escapeJson(r.snippet) + "\"";

        json += "}";
        if (i + 1 < results.size()) json += ",";
    }


    json += "] }";
    return json;
}

// ---------------- MAIN ----------------
int main() {
    SearchEngine engine;
    httplib::Server server;

    // STEP 1: Initialize folders (Create them if they don't exist)
    fs::create_directory("../runtime_corpus");
    fs::create_directory("../database");

    // STEP 2: Try to load the index from the database folder
    string indexPath = "../database/search_index.bin";
    
    if (fs::exists(indexPath)) {
        cout << "Found existing index. Loading via mmap..." << endl;
        engine.loadIndex(indexPath);
        
        // NEW: Delete any files that were uploaded but never saved!
        engine.cleanupOrphanFiles(); 
        
    } else {
        cout << "No existing index found. Starting fresh." << endl;
        
        // NEW: If there is no index at all, everything in runtime_corpus is an orphan. Wipe it!
        for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
            if (entry.is_regular_file()) {
                fs::remove(entry.path());
            }
        }
    }


    server.Options("/upload", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 200; // Tell the browser "Yes, you are allowed to send the POST request!"
    });


// -------- Upload Endpoint --------
    server.Post("/upload", [&](const httplib::Request& req,
                            httplib::Response& res) {

        res.set_header("Access-Control-Allow-Origin", "*");

        if (req.body.empty()) {
            res.set_content("Empty file content", "text/plain");
            return;
        }

        if (!req.has_param("filename")) {
            res.set_content("Missing filename", "text/plain");
            return;
        }

        string docName = req.get_param_value("filename");
        string baseName = docName;

        // FIX: Calculate name and extension HERE, outside the loop!
        size_t dotPos = baseName.find_last_of('.');
        string name = baseName;
        string extension = "";

        if (dotPos != string::npos) {
            name = baseName.substr(0, dotPos);
            // Convert extension to lowercase to safely handle ".PDF" vs ".pdf"
            string extRaw = baseName.substr(dotPos);
            for(char c : extRaw) extension += tolower(c); 
        }

        string savePath = "../runtime_corpus/" + baseName;
        int counter = 1;

        // Loop to handle duplicate filenames
        while (fs::exists(savePath)) {
            savePath = "../runtime_corpus/" + name + "_" + to_string(counter) + extension;
            counter++;
        }

        // NEW: Add ios::binary flag so PDF bytes aren't scrambled on save
        ofstream out(savePath, ios::binary);
        out.write(req.body.data(), req.body.size()); // Much safer for binary PDFs
        out.close();

        string fileToIndex = savePath;

        // NEW: PDF Processing Logic
        if (extension == ".pdf") {
            // Define where the extracted text will be saved
            string txtPath = "../runtime_corpus/" + name + "_" + to_string(counter) + ".txt";

            // Construct shell command: pdftotext "input.pdf" "output.txt"
            string command = "pdftotext \"" + savePath + "\" \"" + txtPath + "\"";
            
            // Execute command
            int result = std::system(command.c_str());

            if (result != 0) {
                res.set_content("Failed to parse PDF. Is 'poppler-utils' installed?", "text/plain");
                return;
            }

            // Tell the engine to index the extracted text file, NOT the binary PDF
            fileToIndex = txtPath;
        }

        // Send the correct file to the engine
        engine.indexSingleDocument(fileToIndex);

        res.set_content("File uploaded and indexed successfully!", "text/plain");
    });


    // -------- Load Sample Dataset --------
    server.Get("/loadSample", [&](const httplib::Request& req,
                              httplib::Response& res) {

        engine.loadSampleDataset();

        string json = "{";
        json += "\"indexing_time_ms\":" +
                to_string(engine.getLastIndexingTime()) + ",";
        json += "\"threads_used\":" +
                to_string(engine.getLastThreadCount());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });


    // -------- Search Endpoint --------
    server.Get("/search", [&](const httplib::Request& req,
                            httplib::Response& res) {

        if (!req.has_param("q")) {
            res.set_content("Missing query", "text/plain");
            return;
        }

        auto q = req.get_param_value("q");


        int page = req.has_param("page") ? stoi(req.get_param_value("page")) : 1;
        int limit = req.has_param("limit") ? stoi(req.get_param_value("limit")) : 10;

        // Start timer
        auto start = std::chrono::high_resolution_clock::now();

        auto results = engine.searchAPI(q, page, limit);

        // End timer
        auto end = std::chrono::high_resolution_clock::now();

        double latency =
            std::chrono::duration<double, std::milli>(end - start).count();

        // Build JSON
        string resultsJson = toJson(results);

        // Inject latency into JSON
        string finalJson = "{";
        finalJson += "\"latency_ms\":" + to_string(latency) + ",";
        finalJson += resultsJson.substr(1); // remove first '{'

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(finalJson, "application/json");
    });

    
    // -------- Autocomplete Endpoint --------
    server.Get("/autocomplete", [&](const httplib::Request& req,
                                    httplib::Response& res) {

        if (!req.has_param("prefix")) {
            res.set_content("Missing prefix", "text/plain");
            return;
        }

        auto prefix = req.get_param_value("prefix");
        auto words = engine.autocompleteAPI(prefix);

        string json = "{ \"suggestions\": [";
        for (size_t i = 0; i < words.size(); i++) {
            json += "\"" + words[i] + "\"";
            if (i + 1 < words.size()) json += ",";
        }
        json += "] }";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });



    // -------- Corpus Info Endpoint --------
    server.Get("/corpusInfo", [&](const httplib::Request& req,
                                httplib::Response& res) {

        string json = "{";
        json += "\"documents\":" + to_string(engine.getDocumentCount()) + ",";
        json += "\"vocabulary\":" + to_string(engine.getVocabularySize());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });


    

    // -------- Clear Corpus --------
    server.Post("/clearCorpus", [&](const httplib::Request& req, httplib::Response& res) {

        namespace fs = std::filesystem;

        // 1. Clear the live in-memory index
        engine.clearIndex();

        // 2. Delete all raw uploaded files (The Bookshelf)
        for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
            if (entry.is_regular_file()) {
                fs::remove(entry.path());
            }
        }

        // 3. NEW: Delete the binary index file on disk (The Card Catalog)
        string indexPath = "../database/search_index.bin";
        if (fs::exists(indexPath)) {
            fs::remove(indexPath);
        }

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Corpus and Index cleared successfully", "text/plain");
    });




    // -------- Rebuild Index --------
    server.Post("/rebuildIndex", [&](const httplib::Request& req,
                                    httplib::Response& res) {

        engine.scanCorpusFolders();   // refresh document list based on mode
        engine.buildIndex();          // rebuild index structures

        string json = "{";
        json += "\"indexing_time_ms\":" +
                to_string(engine.getLastIndexingTime()) + ",";
        json += "\"threads_used\":" +
                to_string(engine.getLastThreadCount());
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });      


    server.Get("/benchmark", [&](const httplib::Request& req,
                             httplib::Response& res) {

        namespace fs = std::filesystem;

        vector<string> filePaths;

        // Scan permanent corpus
        for (const auto& entry : fs::directory_iterator("../documents")) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                filePaths.push_back(entry.path().string());
            }
        }

        // Scan runtime corpus
        for (const auto& entry : fs::directory_iterator("../runtime_corpus")) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".txt") {
                filePaths.push_back(entry.path().string());
            }
        }

        if (filePaths.empty()) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content("No documents found for benchmark", "text/plain");
            return;
        }

        // ---------- SINGLE THREAD ----------
        SearchEngine singleEngine;

        for (auto& path : filePaths)
            singleEngine.addDocument(path);

        auto start1 = std::chrono::high_resolution_clock::now();
        singleEngine.buildIndexSingleThread();
        auto end1 = std::chrono::high_resolution_clock::now();

        double singleTime =
            std::chrono::duration<double, std::milli>(end1 - start1).count();


        // ---------- MULTI THREAD ----------
        SearchEngine multiEngine;

        for (auto& path : filePaths)
            multiEngine.addDocument(path);

        auto start2 = std::chrono::high_resolution_clock::now();
        multiEngine.buildIndex();
        auto end2 = std::chrono::high_resolution_clock::now();

        double multiTime =
            std::chrono::duration<double, std::milli>(end2 - start2).count();

        double speedup = singleTime / multiTime;

        string json = "{";
        json += "\"single_thread_ms\":" + to_string(singleTime) + ",";
        json += "\"multi_thread_ms\":" + to_string(multiTime) + ",";
        json += "\"threads_used\":" + to_string(multiEngine.getLastThreadCount()) + ",";
        json += "\"speedup\":" + to_string(speedup);
        json += "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(json, "application/json");
    });



    // -------- Save Index Endpoint --------
    server.Post("/saveIndex", [&](const httplib::Request& req, httplib::Response& res) {
        // NEW: Save to the dedicated database folder
        engine.saveIndex("../database/search_index.bin");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Index successfully saved to disk!", "text/plain");
    });


    cout << "Dynamic Search Engine running at http://localhost:8080\n";
    server.listen("localhost", 8080);
}











