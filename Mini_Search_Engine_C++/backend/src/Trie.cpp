#include "Trie.h"

Trie::Trie() {
    root = new TrieNode();
}

void Trie::insert(const string& word) {
    TrieNode* curr = root;
    for (char c : word) {
        if (curr->children.find(c) == curr->children.end()) {
            curr->children[c] = new TrieNode();
        }
        curr = curr->children[c];
    }
    curr->isEnd = true;
}

vector<string> Trie::autocomplete(const string& prefix) {
    TrieNode* curr = root;
    for (char c : prefix) {
        if (curr->children.find(c) == curr->children.end())
            return {};
        curr = curr->children[c];
    }

    vector<string> results;
    dfs(curr, prefix, results);
    return results;
}

void Trie::dfs(TrieNode* node, string current, vector<string>& results) {
    if (!node) return;

    if (node->isEnd)
        results.push_back(current);

    for (auto& [ch, child] : node->children) {
        if (child)
            dfs(child, current + ch, results);
    }
}
