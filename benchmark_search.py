import requests
import time
import statistics

BASE_URL = "http://localhost:8080/search?q="

queries = [
    "data",
    "search engine",
    "algorithm",
    "computer science",
    "bm25",
    "thread",
    "index",
    "network",
    "database",
    "machine learning",
] * 20   # 200 queries total

latencies = []

start_total = time.time()

for q in queries:

    start = time.time()

    response = requests.get(BASE_URL + q)

    end = time.time()

    latency_ms = (end - start) * 1000

    latencies.append(latency_ms)

end_total = time.time()

# Metrics
avg_latency = statistics.mean(latencies)
min_latency = min(latencies)
max_latency = max(latencies)

p95_latency = sorted(latencies)[int(0.95 * len(latencies))]

throughput = len(queries) / (end_total - start_total)

print("\n===== SEARCH BENCHMARK =====")
print(f"Queries Run       : {len(queries)}")
print(f"Average Latency   : {avg_latency:.3f} ms")
print(f"Min Latency       : {min_latency:.3f} ms")
print(f"Max Latency       : {max_latency:.3f} ms")
print(f"P95 Latency       : {p95_latency:.3f} ms")
print(f"Throughput        : {throughput:.2f} queries/sec")