#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Student {
    int id{};
    std::string name;
    int age{};
    std::string grade;
};

struct StoreMetrics {
    size_t records{};
    long long load_us{};
    long long save_us{};
    long long sort_us{};
};

class StudentStore {
public:
    explicit StudentStore(std::string csv_path) : path_(std::move(csv_path)) {}

    void load() {
        const auto start = clock::now();
        std::ifstream input(path_);
        if (!input) throw std::runtime_error("Cannot open CSV: " + path_);
        
        std::string line;
        if (!std::getline(input, line) || line != "id,name,age,grade") {
            throw std::runtime_error("CSV must start with id,name,age,grade");
        }
            
        std::vector<Student> loaded;
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            
            const auto cells = parse_csv(line);
            if (cells.size() != 4) throw std::runtime_error("Malformed CSV row: " + line);
            
            Student s{std::stoi(cells[0]), cells[1], std::stoi(cells[2]), cells[3]};
            validate(s);
            
            if (std::any_of(loaded.begin(), loaded.end(), [&](const Student& x) { return x.id == s.id; })) {
                throw std::runtime_error("Duplicate student id: " + std::to_string(s.id));
            }
                
            loaded.push_back(std::move(s));
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        students_ = std::move(loaded);
        metrics_.records = students_.size();
        metrics_.load_us = elapsed(start);
    }

    std::vector<Student> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return students_;
    }
    
    std::optional<Student> find_by_id(int id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(students_.begin(), students_.end(), [&](const Student& s) { return s.id == id; });
        return it == students_.end() ? std::nullopt : std::optional<Student>(*it);
    }
    
    std::vector<Student> search(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Student> result;
        const auto lower = lowercase(needle);
        
        for (const auto& s : students_) {
            if (lowercase(s.name).find(lower) != std::string::npos || 
                std::to_string(s.id).find(lower) != std::string::npos) {
                result.push_back(s);
            }
        }
        return result;
    }
    
    void add(const Student& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        validate(s);
        if (contains(s.id)) throw std::runtime_error("Student id already exists");
        
        students_.push_back(s);
        save_locked();
    }
    
    void update(const Student& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        validate(s);
        auto it = std::find_if(students_.begin(), students_.end(), [&](const Student& x) { return x.id == s.id; });
        if (it == students_.end()) throw std::runtime_error("Student id not found");
        
        *it = s;
        save_locked();
    }
    
    void remove(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(students_.begin(), students_.end(), [&](const Student& s) { return s.id == id; });
        if (it == students_.end()) throw std::runtime_error("Student id not found");
        
        students_.erase(it, students_.end());
        save_locked();
    }
    
    void sort_by_name() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto start = clock::now();
        
        std::sort(students_.begin(), students_.end(), [](const Student& a, const Student& b) { 
            return lowercase(a.name) < lowercase(b.name); 
        });
        
        metrics_.sort_us = elapsed(start);
        save_locked();
    }
    
    void sort_by_id() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto start = clock::now();
        
        std::sort(students_.begin(), students_.end(), [](const Student& a, const Student& b) { 
            return a.id < b.id; 
        });
        
        metrics_.sort_us = elapsed(start);
        save_locked();
    }
    
    void sort_by_grade() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto start = clock::now();
        
        std::sort(students_.begin(), students_.end(), [](const Student& a, const Student& b) { 
            return grade_value(a.grade) < grade_value(b.grade); 
        });
        
        metrics_.sort_us = elapsed(start);
        save_locked();
    }
    
    void replace_all(const std::vector<Student>& values) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : values) validate(s);
        students_ = values;
        save_locked();
    }
    
    StoreMetrics metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_;
    }

private:
    using clock = std::chrono::steady_clock;
    
    static long long elapsed(clock::time_point begin) {
        return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - begin).count();
    }
    
    static std::string lowercase(std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
        return v;
    }
    
    static int grade_value(const std::string& grade) {
        if (grade.empty()) return 1000;
        int value = 0;
        char letter = std::toupper(grade[0]);
        if (letter >= 'A' && letter <= 'F') {
            value = (letter - 'A') * 10;
        } else {
            return 1000;
        }
        if (grade.size() > 1) {
            if (grade[1] == '+') value -= 2;
            else if (grade[1] == '-') value += 2;
        }
        return value;
    }
    
    static void validate(const Student& s) {
        if (s.id <= 0 || s.name.empty() || s.age < 1 || s.age > 150 || s.grade.empty()) {
            throw std::runtime_error("Invalid student: id/name/age/grade are required");
        }
    }
    
    bool contains(int id) const {
        return std::any_of(students_.begin(), students_.end(), [&](const Student& s) { return s.id == id; });
    }
    
    static std::vector<std::string> parse_csv(const std::string& row) {
        std::vector<std::string> cells;
        std::string cell;
        bool quoted = false;
        
        for (size_t i = 0; i < row.size(); ++i) {
            char c = row[i];
            if (c == '"') {
                if (quoted && i + 1 < row.size() && row[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    quoted = !quoted;
                }
            } else if (c == ',' && !quoted) {
                cells.push_back(cell);
                cell.clear();
            } else {
                cell += c;
            }
        }
        
        if (quoted) throw std::runtime_error("Unclosed CSV quote");
        cells.push_back(cell);
        return cells;
    }
    
    static std::string escape_csv(const std::string& text) {
        if (text.find_first_of(",\"") == std::string::npos) return text;
        
        std::string out = "\"";
        for (char c : text) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        return out + "\"";
    }
    
    void save_locked() {
        auto start = clock::now();
        std::ofstream output(path_, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot save CSV: " + path_);
        
        output << "id,name,age,grade\n";
        for (const auto& s : students_) {
            output << s.id << ',' << escape_csv(s.name) << ',' 
                   << s.age << ',' << escape_csv(s.grade) << '\n';
        }
        
        if (!output) throw std::runtime_error("Failed while saving CSV");
        
        metrics_.records = students_.size();
        metrics_.save_us = elapsed(start);
    }

    std::string path_;
    mutable std::mutex mutex_;
    std::vector<Student> students_;
    StoreMetrics metrics_;
};
