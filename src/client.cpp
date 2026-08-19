#include "student_store.hpp"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace asio = boost::asio;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r;
}

static std::string one(const std::string& type, int id, const std::string& name = "", 
                       int age = 0, const std::string& grade = "") {
    std::string r = "{\"type\":\"" + type + "\",\"id\":" + std::to_string(id);
    if (type != "delete") {
        r += ",\"name\":\"" + esc(name) + "\",\"age\":" + std::to_string(age) + 
             ",\"grade\":\"" + esc(grade) + "\"";
    }
    return r + "}";
}

static std::string seed_message(const std::vector<Student>& values) {
    std::string r = "{\"type\":\"replace_all\",\"students\":[";
    for (size_t i = 0; i < values.size(); ++i) {
        const auto& s = values[i];
        if (i > 0) r += ',';
        r += "{\"id\":" + std::to_string(s.id) + 
             ",\"name\":\"" + esc(s.name) + 
             "\",\"age\":" + std::to_string(s.age) + 
             ",\"grade\":\"" + esc(s.grade) + "\"}";
    }
    return r + "]}";
}

int main(int argc, char** argv) {
    try {
        std::string host = argc > 1 ? argv[1] : "127.0.0.1";
        std::string csv = argc > 2 ? argv[2] : "students.csv";
        unsigned short port = argc > 3 ? static_cast<unsigned short>(std::stoi(argv[3])) : 8080;
        
        StudentStore seed(csv);
        seed.load();
        
        asio::io_context io;
        tcp::resolver resolver(io);
        websocket::stream<tcp::socket> ws(io);
        
        auto results = resolver.resolve(host, std::to_string(port));
        asio::connect(ws.next_layer(), results);
        ws.handshake(host + ":" + std::to_string(port), "/");
        
        const auto started = std::chrono::steady_clock::now();
        ws.write(asio::buffer(seed_message(seed.list())));
        const auto send_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
            
        std::cout << "Connected and sent " << seed.list().size() << " startup records in " 
                  << send_us << " us.\nCommands: add id name age grade | update id name age grade | delete id | sort | quit\n";
                  
        std::thread reader([&]() {
            try {
                for (;;) {
                    boost::beast::flat_buffer b;
                    ws.read(b);
                    std::cout << "\n[server] " << boost::beast::buffers_to_string(b.data()) << "\n> " << std::flush;
                }
            } catch (...) {}
        });
        reader.detach();
        
        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            std::istringstream in(line);
            std::string op, name, grade;
            int id, age;
            
            if (!(in >> op)) continue;
            if (op == "quit") break;
            
            std::string msg;
            if (op == "delete" && in >> id) {
                msg = one(op, id);
            } else if ((op == "add" || op == "update") && in >> id >> name >> age >> grade) {
                // "add" on CLI maps to "create" in the WebSocket protocol
                msg = one(op == "add" ? "create" : "update", id, name, age, grade);
            } else if (op == "sort") {
                msg = "{\"type\":\"sort\"}";
            } else {
                std::cout << "Invalid command\n";
                continue;
            }
            ws.write(asio::buffer(msg));
        }
        
        ws.close(websocket::close_code::normal);
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << '\n';
        return 1;
    }
}
