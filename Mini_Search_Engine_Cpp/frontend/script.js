const input = document.getElementById("query");
const output = document.getElementById("output");
const suggestions = document.getElementById("suggestions");
const uploadStatus = document.getElementById("uploadStatus");



// ========================
// 🔹 Upload file
// ========================
function uploadFile() {
  const fileInput = document.getElementById("fileInput");
  const file = fileInput.files[0];

  if (!file) {
    uploadStatus.innerText = "Please select a file.";
    uploadStatus.style.color = "red";
    return;
  }

  const reader = new FileReader();

  reader.onload = function (e) {
    // NEW: Show a loading message (PDF extraction takes a moment)
    uploadStatus.innerText = "Uploading and processing... please wait.";
    uploadStatus.style.color = "#facc15"; // Yellow color

    fetch("/api/upload?filename=" + encodeURIComponent(file.name), {
      method: "POST",
      headers: {
        // NEW: Dynamically set Content-Type
        "Content-Type": file.name.endsWith(".pdf") ? "application/pdf" : "text/plain"
      },
      body: e.target.result // Sends the raw binary array buffer
    })
    .then(res => {
      if (!res.ok) {
        throw new Error("Server error");
      }
      return res.text();
    })
    .then(msg => {
      uploadStatus.innerText = msg;
      // If the backend sends an error string about poppler, turn it red
      uploadStatus.style.color = msg.includes("Failed") ? "red" : "#22c55e";

      fileInput.value = "";
      updateCorpusInfo();
    })
    .catch(err => {
      console.error("Upload error:", err);
      uploadStatus.innerText = "Upload failed!";
      uploadStatus.style.color = "red";
    });
  };

  reader.onerror = function () {
    uploadStatus.innerText = "Failed to read file.";
    uploadStatus.style.color = "red";
  };

  // NEW: Read as ArrayBuffer so PDF bytes are not corrupted!
  reader.readAsArrayBuffer(file);
}




// ========================
// 🔹 WORD BASED HIGHLIGHT
// ========================
function highlightSnippet(text, query) {

  let words = query.toLowerCase().split(" ");

  words.forEach(word => {

    if (!word) return;

    const regex = new RegExp(`\\b(${word})\\b`, "gi");

    text = text.replace(regex, match => `<mark>${match}</mark>`);
  });

  return text;
}





let currentPage = 1;
const resultsPerPage = 10;
let currentQuery = "";



// ========================
// 🔹 Search
// ========================
function search(isNewSearch = true) {
  const q = input.value.trim();
  if (!q) return;


  if (isNewSearch) {
    currentPage = 1;
    currentQuery = q;
  }

  fetch(`/api/search?q=${encodeURIComponent(currentQuery)}&page=${currentPage}&limit=${resultsPerPage}`)
    .then(res => res.json())
    .then(data => {
      output.innerHTML = `<p style="color:#94a3b8">Search time: <b>${data.latency_ms.toFixed(3)} ms</b></p>`;

      if (!data.results || data.results.length === 0) {
        output.innerHTML += "<p class='no-result'>No results found</p>";
        document.getElementById("pagination").style.display = "none";
        return;
      }

      if(data.results[0].suggestion){
        output.innerHTML += `
          <p style="color:#facc15">
            Did you mean:
            <b onclick="query.value='${data.results[0].suggestion}'; search()">
              ${data.results[0].suggestion}
            </b> ?
          </p>
        `;
      }


      data.results.forEach(r => {
        const card = document.createElement("div");
        card.className = "result-card";

        const highlighted = highlightSnippet(r.snippet, input.value);

        card.innerHTML = `
          <h3>${r.document}</h3>
          <p><b>Frequency:</b> ${r.frequency}</p>
          <p><b>Score:</b> ${r.score.toFixed(4)}</p>
          <p class="snippet">${highlighted}...</p>
        `;
        
        output.appendChild(card);
      });


      document.getElementById("pagination").style.display = "flex";
      document.getElementById("pageIndicator").innerText = `Page ${currentPage}`;
      document.getElementById("prevBtn").disabled = (currentPage === 1);
      
      // If we got fewer results than the limit, we hit the end
      document.getElementById("nextBtn").disabled = (data.results.length < resultsPerPage);
    })
    .catch(err => {
      console.error("Search error:", err);
      output.innerHTML = "<p class='no-result'>Search failed</p>";
    });
}


function changePage(direction) {
  currentPage += direction;
  if (currentPage < 1) currentPage = 1;
  search(false); // false means don't reset to page 1
}






// ========================
// 🔹 Autocomplete
// ========================
input.addEventListener("input", () => {
  const q = input.value.trim();

  if (!q) {
    suggestions.innerHTML = "";
    return;
  }

  fetch(`/api/autocomplete?prefix=${encodeURIComponent(q)}`)
    .then(res => res.json())
    .then(data => {
      suggestions.innerHTML = "";

      if (!data.suggestions) return;

      data.suggestions.slice(0, 5).forEach(word => {
        const div = document.createElement("div");
        div.className = "suggestion";
        div.innerText = word;

        div.onclick = () => {
          input.value = word;
          suggestions.innerHTML = "";
        };

        suggestions.appendChild(div);
      });
    })
    .catch(err => {
      console.error("Autocomplete error:", err);
    });
});





function updateCorpusInfo() {
  // FIX: Append a unique timestamp (?t=...) to completely bypass browser caching
  fetch("/api/corpusInfo?t=" + new Date().getTime())
    .then(res => res.json())
    .then(data => {
      document.getElementById("corpusStats").innerText =
        `Documents: ${data.documents} | Vocabulary: ${data.vocabulary}`;
    });
}



function loadInitialCorpus() {

  if (!confirm("This will replace the current corpus. Continue?")) {
    return;
  }

  fetch("/api/loadSample")
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Index built in ${data.indexing_time_ms.toFixed(3)} ms using ${data.threads_used} threads`;

      updateCorpusInfo();
    })
    .catch(err => {
      console.error("Load error:", err);
    });
}



function rebuildIndex() {
  fetch("/api/rebuildIndex", { method: "POST" })
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Index rebuilt in ${data.indexing_time_ms.toFixed(3)} ms using ${data.threads_used} threads`;

      updateCorpusInfo();
    });
}



function clearCorpus() {
  fetch("/api/clearCorpus", { method: "POST" })
    .then(res => res.text())
    .then(msg => {
      alert(msg);
      document.querySelector("button[onclick='loadInitialCorpus()']").disabled = false;
      updateCorpusInfo();
    });
}





function runBenchmark() {

  fetch("/api/benchmark")
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Single-thread: ${data.single_thread_ms.toFixed(3)} ms | ` +
        `Multi-thread: ${data.multi_thread_ms.toFixed(3)} ms | ` +
        `Threads: ${data.threads_used} | ` +
        `Speedup: ${data.speedup.toFixed(2)}x`;
    })
    .catch(err => {
      console.error("Benchmark error:", err);
    });
}


window.onload = updateCorpusInfo;







// ========================
// 🔹 SAVE INDEX TO DISK
// ========================
function saveIndexToDisk() {
  fetch("/api/saveIndex", { method: "POST" })
    .then(res => res.text())
    .then(msg => {
      alert("✅ " + msg); // Pops up a success message on the screen!
    })
    .catch(err => {
      console.error("Error saving index:", err);
      alert("❌ Failed to save index.");
    });
}







// ========================
// 🔹 RAG AI Chat (FastAPI)
// ========================
const chatInput = document.getElementById("chatInput");
const chatHistory = document.getElementById("chatHistory");

function appendMessage(text, senderClass) {
  const msgDiv = document.createElement("div");
  msgDiv.className = `chat-msg ${senderClass}`;
  msgDiv.innerText = text;
  chatHistory.appendChild(msgDiv);
  chatHistory.scrollTop = chatHistory.scrollHeight; // Auto-scroll to bottom
  return msgDiv;
}

function sendChatMessage() {
  const question = chatInput.value.trim();
  if (!question) return;

  // 1. Add User message to UI
  appendMessage(question, "msg-user");
  chatInput.value = "";

  // 2. Add Loading state
  const loadingMsg = appendMessage("AI is reading the C++ database...", "msg-ai msg-loading");

  // 3. Hit the Python FastAPI backend
  fetch("/api/chat", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ question: question })
  })
  .then(res => {
    if (!res.ok) throw new Error("AI Backend Error");
    return res.json();
  })
  .then(data => {
    // Remove loading message and append actual answer
    chatHistory.removeChild(loadingMsg);
    appendMessage(data.answer, "msg-ai");
  })
  .catch(err => {
    console.error(err);
    chatHistory.removeChild(loadingMsg);
    appendMessage("❌ Failed to connect to Python AI Gateway.", "msg-ai");
  });
}

// Allow pressing "Enter" to send
chatInput.addEventListener("keypress", function(event) {
  if (event.key === "Enter") {
    sendChatMessage();
  }
});



