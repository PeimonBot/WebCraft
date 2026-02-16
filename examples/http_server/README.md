# WebCraft HTTP Server Example

Minimal HTTP server built on WebCraft async TCP APIs, with **reverse proxy** and **round-robin load balancing**.

## Features

### 1. Reverse proxy (`proxy_pass`)

- Forwards the client request to a configured backend (upstream) and streams the backend response back to the client.
- Request is sent as-is (method, path, headers, body); the proxy does not rewrite `Host` (backends see the original request).

### 2. Round-robin load balancing

- Each `upstream` can list multiple `server` entries.
- Requests are distributed across backends using a simple round-robin: an atomic counter per upstream selects the next server in order.

### 3. Configuration file (nginx-like subset)

- **File**: pass path as first argument, e.g. `./http_server server.conf`, or default `server.conf` in the current directory.
- **Syntax**:
  - `upstream name { server host:port; server host:port; ... }` — defines a named group of backends.
  - `server { listen port; location /path { proxy_pass http://upstream_name; } location / { } }` — listen port and path-based routes.
- **Matching**: longest path prefix wins. Routes without `proxy_pass` get the built-in “Hello from WebCraft” response.
- **Example**: see `server.conf` in this directory.

If the config file is missing or invalid, the server falls back to listening on 8080 and serving the local response only.

## Build and run

From the project root:

```bash
cmake -S . -B build -DWEBCRAFT_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/http_server/http_server
# Or with config:
./build/examples/http_server/http_server /path/to/server.conf
```

Run backends (e.g. for `server.conf`) on ports 9000 and 9001, then hit `http://localhost:8080/api/` to see round-robin proxying.

## Benchmarking

- **wrk** and **ab** (ApacheBench) are common HTTP load generators. They are not installed in this environment by default.
- To install (Debian/Ubuntu): `sudo apt install wrk` and/or `sudo apt install apache2-utils` (for `ab`).
- **Alternatives**:
  - **curl** in a loop: `for i in $(seq 1 100); do curl -s -o /dev/null http://localhost:8080/; done`
  - **socat** or scripts (e.g. bash + curl) for simple concurrency.

Example with `wrk` (if installed):

```bash
wrk -t4 -c100 -d10s http://localhost:8080/
```

Example with `ab`:

```bash
ab -n 10000 -c 100 http://localhost:8080/
```

## Performance notes

- **Single-threaded async**: One listener, one event loop; all I/O is asynchronous (e.g. io_uring on Linux). Good for many concurrent connections without a large thread count.
- **Proxy path**: Each proxied request opens a new TCP connection to the backend. Reusing connections (HTTP keep-alive to backends) would reduce latency and load under high RPS.
- **Buffering**: 8 KB buffers for request/response streaming; tuning buffer size and batching may help under heavy load.
- **Round-robin**: Lock-free atomic counter; minimal overhead. No health checks or weights; adding backend health checks would improve robustness under failures.
