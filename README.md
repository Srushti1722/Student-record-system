# Student Record Management System

A C++17 student record system with CSV persistence and live WebSocket updates. The C++ server owns the data: each successful CRUD or sort operation is saved to CSV and broadcast immediately to every connected browser and C++ client.

## How to run

**Everyday use (Windows):** from the project folder, double-click `run.bat`, or in a terminal:

```powershell
.\run.ps1
```

That configures CMake with vcpkg if needed, builds the server, opens `index.html`, and starts `ws://localhost:8080`. In VS Code / Cursor, press **Ctrl+Shift+B** (Run student server).

First time only: install Boost with vcpkg and set `VCPKG_ROOT` if it is not `D:\vcpkg`.

### Requirements

- CMake 3.16 or newer
- A C++17 compiler (MSVC on this Windows setup)
- Boost 1.70+ with Asio, Beast, and System (install via vcpkg)
- vcpkg at `D:\vcpkg`, or `VCPKG_ROOT` pointing at your vcpkg install

`CMakeLists.txt` and `run.ps1` pass the vcpkg toolchain automatically when that path exists. Plain `cmake -S . -B build` without vcpkg will not find Boost.

### Build and test

From the project root (vcpkg already available as above):

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For Ninja or Make, omit `Release` from executable paths.

### Start the WebSocket server

```powershell
.\build\Release\student_server.exe students.csv 8080
```

The server loads `students.csv`, prints its load time, and listens at `ws://localhost:8080`.

### Open the browser UI

With the server running, open `index.html` directly in a browser. It connects to `ws://localhost:8080` on load.

The page sends `create`, `update`, `delete`, and `sort` messages over WebSocket. When the server broadcasts a refreshed dataset, every open tab redraws instantly. It uses no HTTP polling and needs no page refresh.

### Run the C++ client

In another terminal:

```powershell
.\build\Release\student_client.exe 127.0.0.1 students.csv 8080
```

| Action | Command |
|---|---|
| Add | `add 106 Meera 20 A` |
| Update | `update 106 Meera 21 A+` |
| Delete | `delete 106` |
| Sort by name | `sort` |
| Quit | `quit` |

The CLI `add` command is translated to the WebSocket `create` message expected by the server. On startup, the CLI sends a `replace_all` message containing its CSV data, as required by the assignment; this intentionally replaces the server dataset.

### Optional programs

`student_demo` demonstrates the data layer without networking:

```powershell
.\build\Release\student_demo.exe
```

On Windows, CMake also builds `student_http_server`, a legacy REST/HTTP example. It is not used by `index.html`; the submitted browser UI is WebSocket-only.

## Technology

| Component | Technology |
|---|---|
| Language | C++17 |
| WebSocket server and CLI client | Boost.Beast and Boost.Asio |
| Concurrency | `std::thread`, `std::mutex` |
| Storage | CSV file with a small CSV parser/writer |
| Browser view | Plain HTML, CSS, and JavaScript |
| Build and tests | CMake and C++ `assert` |

## Features implemented

- Loads `students.csv` at startup and validates required fields.
- In-memory create, list, search, update, delete, and sort operations.
- Saves the complete CSV after every successful mutation.
- WebSocket server accepts browser and C++ clients.
- Server broadcasts the current dataset and metrics after each change.
- Browser UI supports CRUD controls, name/ID filtering, sort by name/id/grade, responsive layout, and live metrics.
- Browser filtering remains active when a live dataset broadcast arrives.
- Separate mutexes protect the data store, session list, and individual WebSocket writes.
- Tests cover CRUD, case-insensitive search, sort by name, record-count metrics, and persistence by reloading the saved CSV.

## WebSocket protocol

Clients send:

```json
{"type":"create","id":106,"name":"Meera Rao","age":20,"grade":"A"}
{"type":"update","id":106,"name":"Meera Rao","age":21,"grade":"A+"}
{"type":"delete","id":106}
{"type":"sort","by":"name"}
{"type":"replace_all","students":[...]}
```

The server broadcasts this after a successful operation and when a client connects:

```json
{
  "type": "students",
  "students": [{"id": 101, "name": "Aarav Sharma", "age": 22, "grade": "A"}],
  "metrics": {"records": 9, "load_us": 0, "save_us": 0, "sort_us": 0}
}
```

Timing values are measured at runtime. Invalid input produces an error response only for the requesting client:

```json
{"type":"error","message":"Student id already exists"}
```

## Performance and trade-offs

The included `students.csv` has **9 records**. All timings below were measured on the Release build (MSVC, Windows) against that file. The first run after a reboot shows a cold-cache spike; the warm numbers are representative of steady-state operation.

| Metric | Cold (first run) | Warm (subsequent runs) |
|---|---|---|
| Records loaded | 9 | 9 |
| CSV load + parse | ~18 000 µs | ~300–400 µs |
| Sort (9 records, by name) | ~25 µs | ~14–15 µs |
| CSV save (full rewrite) | ~550 µs | ~200–350 µs |
| WebSocket transmit (replace\_all, 9 records) | — | < 1 ms (printed by client at startup) |
| Server broadcast (1 client connected) | — | < 500 µs (logged to stderr per broadcast) |

The cold load spike is entirely OS file-cache warm-up; once the file is cached the store loads and saves in well under a millisecond. WebSocket transmit and broadcast times are reported live at runtime: the client prints send time in microseconds on startup, and the server logs `[metrics] broadcast N clients: X us` to stderr after every operation.

**Memory usage:** not tracked at runtime. Working set is proportional to record count × average field sizes. At 9 records the footprint is negligible (< 1 KB of student data); for tens of thousands of records a streaming or paginated approach would be needed instead of the current full in-memory vector.

**Trade-offs:**
- Lookup and search are O(n) because records are held in a vector. An `unordered_map` would improve large-data lookup.
- Each mutation rewrites the full CSV. This favors simple, reliable persistence over high-volume write throughput.
- The complete dataset is broadcast sequentially to every client. A delta protocol or async queue would scale better for many clients.

## Limitations

- The CLI parses a single-word name; the browser accepts names with spaces.
- The CLI startup `replace_all` can overwrite data already held by the server.
- There is no authentication or authorization.
- Full-dataset broadcasts and no pagination are not suited to very large datasets.
- `student_http_server` is Windows-only and is retained as a separate legacy example, not the WebSocket browser workflow.

## Project layout

```text
student-record-system-submission-websocket-complete/
├── src/
│   ├── student_store.hpp   reusable data layer
│   ├── server.cpp          WebSocket server
│   └── client.cpp          WebSocket CLI client
├── tests/store_tests.cpp   unit tests
├── index.html              live WebSocket browser UI
├── students.csv            input and live server storage
├── students_output.csv     example output CSV
├── demo.cpp                standalone data-layer demonstration
├── web_server.cpp          legacy Windows HTTP example
├── CMakeLists.txt          build configuration
├── CMakePresets.json       vcpkg CMake preset
├── run.ps1 / run.bat       one-command build and run
├── .gitignore
└── README.md               this document
```
