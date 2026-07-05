#ifndef TRIE_H
#define TRIE_H

#include <unordered_map>
#include <string>
#include <vector>

using namespace std;

class TrieNode {
public:
    unordered_map<char, TrieNode*> children;
    bool isEnd;

    TrieNode() : isEnd(false) {}
};

class Trie {
public:
    Trie();
    void insert(const string& word);
    vector<string> autocomplete(const string& prefix);

private:
    TrieNode* root;
    void dfs(TrieNode* node, string current, vector<string>& results);
};

#endif
