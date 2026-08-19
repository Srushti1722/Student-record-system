#include "student_store.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    const std::string file = "store_test_data.csv";
    {
        std::ofstream f(file);
        f << "id,name,age,grade\n"
          << "1,Alice,20,A\n"
          << "2,Bob,21,B\n";
    }

    StudentStore store(file);
    store.load();
    assert(store.list().size() == 2);

    store.add({3, "Carol", 22, "A-"});
    assert(store.find_by_id(3)->name == "Carol");
    assert(store.search("ALICE").size() == 1); // case-insensitive search

    store.update({3, "Carol Jones", 23, "A"});
    assert(store.search("jOnEs").size() == 1);
    store.remove(2);
    store.sort_by_name();
    assert(store.list().size() == 2);
    assert(store.metrics().records == 2);

    // Reloading proves mutations were persisted to CSV.
    StudentStore reloaded(file);
    reloaded.load();
    const auto saved = reloaded.find_by_id(3);
    assert(saved && saved->name == "Carol Jones" && saved->age == 23);

    std::cout << "All store tests passed\n";
}
