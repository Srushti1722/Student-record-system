#include "student_store.hpp"
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <fstream>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// ─── JSON helpers ────────────────────────────────────────────────────────────
static int getJsonInt(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) throw std::runtime_error("Key not found: " + key);
    pos = json.find(":", pos);
    size_t start = json.find_first_of("-0123456789", pos);
    if (start == std::string::npos) throw std::runtime_error("No value for: " + key);
    size_t end = json.find_first_not_of("0123456789", start + (json[start] == '-' ? 1 : 0));
    return std::stoi(json.substr(start, end == std::string::npos ? std::string::npos : end - start));
}

static std::string getJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) throw std::runtime_error("Key not found: " + key);
    size_t start = pos + pattern.length();
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\\') { end += 2; continue; }
        if (json[end] == '"') break;
        ++end;
    }
    return json.substr(start, end - start);
}

// ─── HTTP response builders ──────────────────────────────────────────────────
static std::string httpOk(const std::string& contentType, const std::string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

static std::string httpError(int code, const std::string& msg) {
    std::string status = code == 400 ? "400 Bad Request" : "404 Not Found";
    std::string body = "{\"error\":\"" + msg + "\"}";
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

// ─── Student JSON serializer ─────────────────────────────────────────────────
static std::string studentsJson(StudentStore& store) {
    auto students = store.list();
    auto metrics  = store.metrics();
    std::string json = "{\"students\":[";
    for (size_t i = 0; i < students.size(); ++i) {
        if (i > 0) json += ",";
        const auto& s = students[i];
        json += "{\"id\":" + std::to_string(s.id)
              + ",\"name\":\"" + s.name + "\""
              + ",\"age\":"  + std::to_string(s.age)
              + ",\"grade\":\"" + s.grade + "\"}";
    }
    json += "],\"metrics\":{"
          "\"records\":"  + std::to_string(metrics.records) +
          ",\"load_us\":" + std::to_string(metrics.load_us) +
          ",\"save_us\":" + std::to_string(metrics.save_us) + "}}";
    return json;
}

// ─── Handle one HTTP connection ──────────────────────────────────────────────
static void handleClient(SOCKET client, StudentStore& store) {
    // Read headers first
    std::string request;
    char buf[8192];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { closesocket(client); return; }
    buf[n] = '\0';
    request = buf;

    // If there's a Content-Length, keep reading until we have the full body
    size_t header_end = request.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        size_t cl_pos = request.find("Content-Length: ");
        if (cl_pos == std::string::npos) cl_pos = request.find("content-length: ");
        if (cl_pos != std::string::npos) {
            size_t cl_end = request.find("\r\n", cl_pos);
            int content_length = std::stoi(request.substr(cl_pos + 16, cl_end - cl_pos - 16));
            int body_received = (int)request.size() - (int)(header_end + 4);
            while (body_received < content_length) {
                char more[4096];
                int m = recv(client, more, sizeof(more) - 1, 0);
                if (m <= 0) break;
                more[m] = '\0';
                request += more;
                body_received += m;
            }
        }
    }

    // Parse first line: METHOD PATH HTTP/1.x
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    if (path == "/") path = "/index.html";

    std::string response;

    try {
        // ── Serve the HTML page ───────────────────────────────────────────
        if (path == "/index.html") {
            std::ifstream file("index.html");
            if (!file) {
                response = httpError(404, "index.html not found");
            } else {
                std::string body((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                response = httpOk("text/html", body);
            }

        // ── OPTIONS pre-flight (browser CORS check) ───────────────────────
        } else if (method == "OPTIONS") {
            response = "HTTP/1.1 204 No Content\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: Content-Type\r\n"
                       "Connection: close\r\n\r\n";

        // ── GET /api/students ─────────────────────────────────────────────
        } else if (path == "/api/students" && method == "GET") {
            response = httpOk("application/json", studentsJson(store));

        // ── POST /api/students ────────────────────────────────────────────
        } else if (path == "/api/students" && method == "POST") {
            size_t body_start = request.find("\r\n\r\n");
            if (body_start == std::string::npos) {
                response = httpError(400, "No body");
            } else {
                std::string body = request.substr(body_start + 4);
                int id     = getJsonInt(body, "id");
                std::string name  = getJsonString(body, "name");
                int age    = getJsonInt(body, "age");
                std::string grade = getJsonString(body, "grade");
                store.add({id, name, age, grade});
                response = httpOk("application/json", "{\"ok\":true}");
            }

        // ── PUT /api/students/<id> ────────────────────────────────────────
        } else if (path.find("/api/students/") == 0 && method == "PUT") {
            int id = std::stoi(path.substr(14));
            size_t body_start = request.find("\r\n\r\n");
            if (body_start == std::string::npos) {
                response = httpError(400, "No body");
            } else {
                std::string body = request.substr(body_start + 4);
                std::string name  = getJsonString(body, "name");
                int age    = getJsonInt(body, "age");
                std::string grade = getJsonString(body, "grade");
                store.update({id, name, age, grade});
                response = httpOk("application/json", "{\"ok\":true}");
            }

        // ── DELETE /api/students/<id> ─────────────────────────────────────
        } else if (path.find("/api/students/") == 0 && method == "DELETE") {
            int id = std::stoi(path.substr(14));
            store.remove(id);
            response = httpOk("application/json", "{\"ok\":true}");

        // ── GET /api/students/search/<query> ──────────────────────────────
        } else if (path.find("/api/students/search/") == 0 && method == "GET") {
            std::string query = path.substr(21);
            auto results = store.search(query);
            std::string json = "{\"students\":[";
            for (size_t i = 0; i < results.size(); ++i) {
                if (i > 0) json += ",";
                const auto& s = results[i];
                json += "{\"id\":"  + std::to_string(s.id)
                      + ",\"name\":\"" + s.name + "\""
                      + ",\"age\":" + std::to_string(s.age)
                      + ",\"grade\":\"" + s.grade + "\"}";
            }
            json += "]}";
            response = httpOk("application/json", json);

        // ── POST /api/students/sort ───────────────────────────────────────
        } else if (path.find("/api/students/sort") == 0 && method == "POST") {
            if (path == "/api/students/sort/id") store.sort_by_id();
            else if (path == "/api/students/sort/grade") store.sort_by_grade();
            else store.sort_by_name();
            response = httpOk("application/json", "{\"ok\":true}");

        } else {
            response = httpError(404, "Not found");
        }
    } catch (const std::exception& e) {
        response = httpError(400, e.what());
    }

    // Send full response
    const char* data = response.c_str();
    int remaining = (int)response.size();
    while (remaining > 0) {
        int sent = send(client, data, remaining, 0);
        if (sent == SOCKET_ERROR) break;
        data      += sent;
        remaining -= sent;
    }

    shutdown(client, SD_SEND);
    closesocket(client);
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::string csv = argc > 1 ? argv[1] : "students.csv";

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    StudentStore store(csv);
    try {
        store.load();
        auto m = store.metrics();
        std::cout << "Loaded " << m.records << " records in " << m.load_us << " us\n";
    } catch (...) {
        std::ofstream f(csv);
        f << "id,name,age,grade\n";
        std::cout << "No CSV found — created empty " << csv << "\n";
    }

    SOCKET listening = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(8000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listening, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        return 1;
    }
    listen(listening, SOMAXCONN);

    std::cout << "Server listening on http://localhost:8000\n";
    std::cout << "Open http://localhost:8000/index.html in your browser\n";

    // Accept loop — each connection gets its own thread
    while (true) {
        SOCKET client = accept(listening, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        std::thread([client, &store]() {
            handleClient(client, store);
        }).detach();
    }

    closesocket(listening);
    WSACleanup();
    return 0;
}
