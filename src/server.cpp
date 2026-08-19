#include "student_store.hpp"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <regex>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace asio = boost::asio;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

static std::string json_escape(const std::string& v) {
    std::string r;
    for (char c : v) {
        if (c == '"' || c == '\\') r += '\\';
        if (c == '\n') r += "\\n";
        else r += c;
    }
    return r;
}

static std::string dataset_json(StudentStore& store) {
    const auto all = store.list();
    const auto metrics = store.metrics();
    std::string r = "{\"type\":\"students\",\"students\":[";
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& s = all[i];
        if (i > 0) r += ',';
        r += "{\"id\":" + std::to_string(s.id) + 
             ",\"name\":\"" + json_escape(s.name) + 
             "\",\"age\":" + std::to_string(s.age) + 
             ",\"grade\":\"" + json_escape(s.grade) + "\"}";
    }
    return r + "],\"metrics\":{\"records\":" + std::to_string(metrics.records) +
           ",\"load_us\":" + std::to_string(metrics.load_us) +
           ",\"save_us\":" + std::to_string(metrics.save_us) +
           ",\"sort_us\":" + std::to_string(metrics.sort_us) + "}}";
}

static std::string get_string(const std::string& j, const std::string& key) {
    std::regex p("\\\"" + key + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
    std::smatch m;
    if (!std::regex_search(j, m, p)) throw std::runtime_error("Missing string field: " + key);
    
    std::string v = m[1];
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            r += (n == 'n') ? '\n' : n;
        } else {
            r += v[i];
        }
    }
    return r;
}

static int get_int(const std::string& j, const std::string& key) {
    std::regex p("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (!std::regex_search(j, m, p)) throw std::runtime_error("Missing number field: " + key);
    return std::stoi(m[1]);
}

static Student message_student(const std::string& j) {
    return {get_int(j, "id"), get_string(j, "name"), get_int(j, "age"), get_string(j, "grade")};
}

class Hub;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, Hub& hub) : ws_(std::move(socket)), hub_(hub) {}
    void run();
    void send(const std::string& message);
    
private:
    websocket::stream<tcp::socket> ws_;
    Hub& hub_;
    std::mutex write_mutex_;
};

class Hub {
public:
    explicit Hub(StudentStore& store) : store_(store) {}
    
    void add(const std::shared_ptr<Session>& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.push_back(s);
    }
    
    void remove(Session* target) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
            [&](const auto& s) { return s.get() == target; }), sessions_.end());
    }
    
    void broadcast() {
        auto start = std::chrono::steady_clock::now();
        const auto body = dataset_json(store_);
        std::vector<std::shared_ptr<Session>> copy;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            copy = sessions_;
        }
        
        for (auto& s : copy) {
            s->send(body);
        }
        
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cerr << "[metrics] broadcast " << copy.size() << " clients: " << us << " us\n";
    }
    
    void handle(const std::string& body) {
        const auto type = get_string(body, "type");
        
        if (type == "create") {
            store_.add(message_student(body));
        } else if (type == "update") {
            store_.update(message_student(body));
        } else if (type == "delete") {
            store_.remove(get_int(body, "id"));
        } else if (type == "sort") {
            std::string by = "name";
            try { by = get_string(body, "by"); } catch(...) {}
            
            if (by == "id") store_.sort_by_id();
            else if (by == "grade") store_.sort_by_grade();
            else store_.sort_by_name();
        } else if (type == "replace_all") {
            std::vector<Student> data;
            std::regex object("\\{[^{}]*\\}");
            for (std::sregex_iterator it(body.begin(), body.end(), object), end; it != end; ++it) {
                if (it->str().find("\"id\"") != std::string::npos) {
                    data.push_back(message_student(it->str()));
                }
            }
            store_.replace_all(data);
        } else {
            throw std::runtime_error("Unknown operation: " + type);
        }
        broadcast();
    }
    
    StudentStore& store() { return store_; }
    
private:
    StudentStore& store_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Session>> sessions_;
};

void Session::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    boost::system::error_code ec;
    ws_.text(true);
    ws_.write(asio::buffer(message), ec);
}

void Session::run() {
    try {
        ws_.accept();
        hub_.add(shared_from_this());
        send(dataset_json(hub_.store()));
        std::cerr << "Client connected\n";
        
        for (;;) {
            boost::beast::flat_buffer b;
            ws_.read(b);
            try {
                hub_.handle(boost::beast::buffers_to_string(b.data()));
            } catch (const std::exception& e) {
                send(std::string("{\"type\":\"error\",\"message\":\"") + json_escape(e.what()) + "\"}");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Client disconnected: " << e.what() << '\n';
    }
    hub_.remove(this);
}

int main(int argc, char** argv) {
    try {
        const std::string csv = argc > 1 ? argv[1] : "students.csv";
        const unsigned short port = argc > 2 ? static_cast<unsigned short>(std::stoi(argv[2])) : 8080;
        
        StudentStore store(csv);
        store.load();
        auto m = store.metrics();
        
        std::cout << "Loaded " << m.records << " records in " << m.load_us << " us\n"
                  << "Listening on ws://localhost:" << port << "\n";
                  
        asio::io_context io;
        tcp::acceptor acceptor(io, {tcp::v4(), port});
        Hub hub(store);
        
        for (;;) {
            tcp::socket socket(io);
            acceptor.accept(socket);
            std::thread([s = std::make_shared<Session>(std::move(socket), hub)]() {
                s->run();
            }).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}
