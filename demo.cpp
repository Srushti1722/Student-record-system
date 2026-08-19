#include "student_store.hpp"
#include <iostream>
#include <iomanip>

void print_student(const Student& s) {
    std::cout << "  ID: " << s.id << " | Name: " << s.name << " | Age: " << s.age << " | Grade: " << s.grade << "\n";
}

int main(){
    std::cout << "===== STUDENT RECORD SYSTEM DEMO =====\n\n";
    
    const std::string file = "demo_data.csv";
    
    // Create initial CSV file
    {
        std::cout << "1. Creating initial CSV file with sample data...\n";
        std::ofstream f(file);
        f << "id,name,age,grade\n";
        f << "1,Alice Smith,20,A\n";
        f << "2,Bob Johnson,21,B+\n";
        f << "3,Carol Davis,19,A-\n";
        std::cout << "   ✓ File created\n\n";
    }
    
    // Load students
    StudentStore store(file);
    std::cout << "2. Loading students from CSV...\n";
    store.load();
    auto metrics = store.metrics();
    std::cout << "   ✓ Loaded " << metrics.records << " records in " << metrics.load_us << " microseconds\n";
    std::cout << "   Current students:\n";
    for (const auto& s : store.list()) print_student(s);
    std::cout << "\n";
    
    // Add new student
    std::cout << "3. Adding new student (ID: 4, David Wilson, Age: 22, Grade: B)...\n";
    store.add({4, "David Wilson", 22, "B"});
    std::cout << "   ✓ Student added\n";
    std::cout << "   Current students:\n";
    for (const auto& s : store.list()) print_student(s);
    std::cout << "\n";
    
    // Search by name
    std::cout << "4. Searching for students with 'smith' in name...\n";
    auto results = store.search("smith");
    std::cout << "   ✓ Found " << results.size() << " match(es):\n";
    for (const auto& s : results) print_student(s);
    std::cout << "\n";
    
    // Find by ID
    std::cout << "5. Finding student by ID (ID: 2)...\n";
    auto found = store.find_by_id(2);
    if (found) {
        std::cout << "   ✓ Found:\n";
        print_student(*found);
    }
    std::cout << "\n";
    
    // Update student
    std::cout << "6. Updating student ID 2 (changing name to 'Robert Johnson' and grade to 'A')...\n";
    store.update({2, "Robert Johnson", 21, "A"});
    std::cout << "   ✓ Student updated\n";
    std::cout << "   Updated student:\n";
    auto updated = store.find_by_id(2);
    if (updated) print_student(*updated);
    std::cout << "\n";
    
    // Sort by name
    std::cout << "7. Sorting students by name...\n";
    store.sort_by_name();
    metrics = store.metrics();
    std::cout << "   ✓ Sorted in " << metrics.sort_us << " microseconds\n";
    std::cout << "   Sorted students:\n";
    for (const auto& s : store.list()) print_student(s);
    std::cout << "\n";
    
    // Remove student
    std::cout << "8. Removing student ID 1 (Alice Smith)...\n";
    store.remove(1);
    std::cout << "   ✓ Student removed\n";
    std::cout << "   Remaining students:\n";
    for (const auto& s : store.list()) print_student(s);
    std::cout << "\n";
    
    // Final metrics
    std::cout << "9. Final metrics:\n";
    metrics = store.metrics();
    std::cout << "   Total records: " << metrics.records << "\n";
    std::cout << "   Load time: " << metrics.load_us << " us\n";
    std::cout << "   Sort time: " << metrics.sort_us << " us\n";
    std::cout << "   Save time: " << metrics.save_us << " us\n\n";
    
    std::cout << "===== DEMO COMPLETED SUCCESSFULLY =====\n";
    return 0;
}
