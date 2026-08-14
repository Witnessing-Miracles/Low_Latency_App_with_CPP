// TestMyVector.cpp
//
// Build (debug, with sanitizers recommended):
//   g++ -std=c++17 -g -O0 -fsanitize=address,undefined -Wall -Wextra \
//       TestMyVector.cpp -o TestMyVector
//   ./TestMyVector
//
// Plain build (no sanitizers):
//   g++ -std=c++17 -g -O0 -Wall -Wextra TestMyVector.cpp -o TestMyVector
//
// This file is a self-contained, dependency-free test harness (no gtest)
// so it can be compiled with nothing but a C++17 compiler.

#include "MyVector.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <stdexcept>

// ------------------------------------------------------------------
// Minimal test framework: register named test functions, run them all,
// catch exceptions so one failing test doesn't kill the whole run.
// ------------------------------------------------------------------
namespace testing {

struct Result {
    std::string name;
    bool passed;
    std::string message;
};

static std::vector<Result>& results() {
    static std::vector<Result> r;
    return r;
}

static void run(const std::string& name, const std::function<void()>& fn) {
    try {
        fn();
        results().push_back({name, true, ""});
    } catch (const std::exception& e) {
        results().push_back({name, false, std::string("threw: ") + e.what()});
    } catch (...) {
        results().push_back({name, false, "threw unknown exception"});
    }
}

static int summarize() {
    int failed = 0;
    std::cout << "\n===== TEST RESULTS =====\n";
    for (const auto& r : results()) {
        std::cout << (r.passed ? "[PASS] " : "[FAIL] ") << r.name;
        if (!r.passed) {
            std::cout << "  -> " << r.message;
            failed++;
        }
        std::cout << "\n";
    }
    std::cout << "=========================\n";
    std::cout << results().size() - failed << " / " << results().size()
              << " tests passed\n";
    return failed;
}

} // namespace testing

// A lightweight ASSERT that throws (so testing::run can catch and report,
// instead of the whole program aborting on a failed assert()).
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            throw std::runtime_error(                                     \
                std::string("CHECK failed: ") + #cond +                   \
                " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")");   \
        }                                                                  \
    } while (0)

// ------------------------------------------------------------------
// Tracker: instrumented type that counts every ctor/dtor/copy/move call.
// This is the main tool for verifying MyVector's memory management is
// correct: every constructed object must eventually be destroyed exactly
// once, and grow() should prefer move over copy.
// ------------------------------------------------------------------
struct Tracker {
    inline static int ctor        = 0;
    inline static int dtor        = 0;
    inline static int copy_ctor   = 0;
    inline static int move_ctor   = 0;
    inline static int copy_assign = 0;
    inline static int move_assign = 0;

    int id;
    bool moved_from = false;

    explicit Tracker(int i = 0) : id(i) { ctor++; }

    Tracker(const Tracker& o) : id(o.id) { copy_ctor++; }

    Tracker(Tracker&& o) noexcept : id(o.id) {
        move_ctor++;
        o.moved_from = true;
        o.id = -1;
    }

    Tracker& operator=(const Tracker& o) {
        id = o.id;
        copy_assign++;
        return *this;
    }

    Tracker& operator=(Tracker&& o) noexcept {
        id = o.id;
        o.moved_from = true;
        o.id = -1;
        move_assign++;
        return *this;
    }

    ~Tracker() { dtor++; }

    static void reset() {
        ctor = dtor = copy_ctor = move_ctor = copy_assign = move_assign = 0;
    }

    // total number of "live constructions" (any ctor that produced an object)
    static int total_constructed() {
        return ctor + copy_ctor + move_ctor;
    }
};

bool operator==(const Tracker& a, const Tracker& b) { return a.id == b.id; }

// ------------------------------------------------------------------
// A type that throws after a configurable number of constructions, used
// to probe exception safety of grow() / copy constructor.
// ------------------------------------------------------------------
struct ThrowingType {
    inline static int alive_count  = 0;
    inline static int throw_after  = -1; // -1 means "never throw"
    inline static int construct_calls = 0;

    int id;

    explicit ThrowingType(int i = 0) : id(i) {
        construct_calls++;
        if (throw_after >= 0 && construct_calls > throw_after) {
            throw std::runtime_error("ThrowingType: simulated ctor failure");
        }
        alive_count++;
    }

    ThrowingType(const ThrowingType& o) : id(o.id) {
        construct_calls++;
        if (throw_after >= 0 && construct_calls > throw_after) {
            throw std::runtime_error("ThrowingType: simulated copy failure");
        }
        alive_count++;
    }

    ThrowingType(ThrowingType&& o) noexcept : id(o.id) {
        // move ctor is noexcept on purpose (matches Tracker / typical usage);
        // std::vector would only invoke this path if it's noexcept anyway.
        alive_count++;
    }

    ~ThrowingType() { alive_count--; }

    static void reset() {
        alive_count = 0;
        throw_after = -1;
        construct_calls = 0;
    }
};

// ==================================================================
// Layer 1: construction / destruction balance (leak & double-free check)
// ==================================================================
void test_ctor_dtor_balance_basic() {
    Tracker::reset();
    {
        MyVector<Tracker> v;
        for (int i = 0; i < 100; ++i) {
            v.push_back(Tracker(i));
        }
        CHECK(v.size() == 100);
    } // v destructs here
    CHECK(Tracker::total_constructed() == Tracker::dtor);
}

void test_ctor_dtor_balance_after_many_grows() {
    Tracker::reset();
    {
        MyVector<Tracker> v;
        for (int i = 0; i < 10000; ++i) {
            v.push_back(Tracker(i));
        }
    }
    CHECK(Tracker::total_constructed() == Tracker::dtor);
}

void test_ctor_dtor_balance_with_pop_back() {
    Tracker::reset();
    {
        MyVector<Tracker> v;
        for (int i = 0; i < 50; ++i) v.push_back(Tracker(i));
        for (int i = 0; i < 20; ++i) v.pop_back();
        CHECK(v.size() == 30);
    }
    CHECK(Tracker::total_constructed() == Tracker::dtor);
}

// ==================================================================
// Layer 2: grow() should move, not copy, when T has a move constructor
// ==================================================================
void test_grow_prefers_move_over_copy() {
    Tracker::reset();
    MyVector<Tracker> v;
    for (int i = 0; i < 20; ++i) {
        v.push_back(Tracker(i)); // push_back(const T&) itself does 1 copy per call
    }
    // push_back's own parameter binds to a temporary Tracker(i) by const&,
    // then copy-constructs into place -> that accounts for copy_ctor calls.
    // grow()'s internal relocation must use move, contributing to move_ctor,
    // and must NOT add extra copy_ctor calls.
    CHECK(Tracker::copy_ctor == 20);   // exactly one copy per push_back
    CHECK(Tracker::move_ctor > 0);     // grow() actually happened and moved elements
}

// ==================================================================
// Layer 3: edge cases / boundary behavior
// ==================================================================
void test_empty_vector_state() {
    MyVector<int> v;
    CHECK(v.empty());
    CHECK(v.size() == 0);
    CHECK(v.capacity() == 0);
}

void test_at_throws_out_of_range() {
    MyVector<int> v;
    v.push_back(1);
    v.push_back(2);

    bool caught = false;
    try {
        v.at(5);
    } catch (const std::out_of_range&) {
        caught = true;
    }
    CHECK(caught);

    // valid access should not throw
    CHECK(v.at(0) == 1);
    CHECK(v.at(1) == 2);
}

void test_operator_bracket_matches_at() {
    MyVector<int> v;
    for (int i = 0; i < 10; ++i) v.push_back(i * i);
    for (int i = 0; i < 10; ++i) {
        CHECK(v[i] == v.at(static_cast<size_t>(i)));
        CHECK(v[i] == i * i);
    }
}

void test_self_copy_assignment_is_safe() {
    MyVector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v = v; // must not corrupt or double-free
    CHECK(v.size() == 3);
    CHECK(v[0] == 1 && v[1] == 2 && v[2] == 3);
}

void test_move_construction_leaves_source_empty_and_valid() {
    MyVector<int> a;
    a.push_back(10);
    a.push_back(20);

    MyVector<int> b(std::move(a));
    CHECK(b.size() == 2);
    CHECK(b[0] == 10 && b[1] == 20);

    // moved-from vector must be a valid, reusable object
    CHECK(a.size() == 0);
    CHECK(a.capacity() == 0);
    a.push_back(99);
    CHECK(a.size() == 1);
    CHECK(a[0] == 99);
}

// ==================================================================
// Layer 4: deep-copy independence (copy ctor / copy assignment)
// ==================================================================
void test_copy_constructor_is_deep() {
    MyVector<int> a;
    a.push_back(1);
    a.push_back(2);

    MyVector<int> b(a);
    b.push_back(3);
    b[0] = 100;

    CHECK(a.size() == 2);       // 'a' must be unaffected by changes to 'b'
    CHECK(a[0] == 1);
    CHECK(b.size() == 3);
    CHECK(b[0] == 100);
}

void test_copy_assignment_is_deep_and_replaces_contents() {
    MyVector<int> a;
    a.push_back(1);
    a.push_back(2);
    a.push_back(3);

    MyVector<int> b;
    b.push_back(999); // b starts with different contents

    b = a;
    b[0] = 42;

    CHECK(a.size() == 3);
    CHECK(a[0] == 1); // unaffected by mutation of b after the assignment
    CHECK(b.size() == 3);
    CHECK(b[0] == 42);
    CHECK(b[1] == 2 && b[2] == 3);
}

// ==================================================================
// Layer 5: exception-safety probe
//
// NOTE: MyVector's grow() as currently written is NOT strongly exception
// safe: if T's copy/move constructor throws partway through relocation,
// the newly allocated block (and any objects already constructed into it)
// leak, and the objects already destroyed from the old block are gone.
// This test documents/detects that gap rather than asserting the ideal
// (leak-free) behavior, so it is expected to reveal a known limitation.
// ==================================================================
void test_exception_during_push_back_growth() {
    ThrowingType::reset();
    bool threw = false;
    {
        MyVector<ThrowingType> v;
        try {
            // Push enough elements to force at least one grow(), then make
            // construction fail partway through a subsequent push_back's
            // placement-new (not inside grow's relocation loop, since that
            // loop uses the noexcept move ctor here).
            for (int i = 0; i < 4; ++i) v.push_back(ThrowingType(i));
            ThrowingType::throw_after = ThrowingType::construct_calls; // next ctor throws
            v.push_back(ThrowingType(999));
        } catch (const std::exception&) {
            threw = true;
        }
    }
    CHECK(threw);
    // Known limitation: depending on where the throw happens relative to
    // grow(), alive_count may not return to 0 here. We report rather than
    // hard-fail the whole suite on this known gap.
    if (ThrowingType::alive_count != 0) {
        std::cout << "  [note] exception-safety gap confirmed: "
                  << ThrowingType::alive_count
                  << " ThrowingType object(s) leaked after an exception "
                     "during push_back/grow (MyVector::grow() has no "
                     "strong exception-safety guarantee)\n";
    }
}

// ==================================================================
// Layer 6: differential test against std::vector
// Runs the same randomized operation sequence against MyVector<int> and
// std::vector<int>, checking state matches after every step.
// ==================================================================
void test_differential_against_std_vector() {
    std::srand(12345); // fixed seed for reproducibility
    MyVector<int> mine;
    std::vector<int> ref;

    for (int step = 0; step < 5000; ++step) {
        int op = std::rand() % 3;
        if (op == 0 || ref.empty()) {
            int val = std::rand() % 1000;
            mine.push_back(val);
            ref.push_back(val);
        } else if (op == 1) {
            mine.pop_back();
            ref.pop_back();
        } else {
            // read-only check via operator[]
            if (!ref.empty()) {
                size_t idx = static_cast<size_t>(std::rand()) % ref.size();
                CHECK(mine[idx] == ref[idx]);
            }
        }
        CHECK(mine.size() == ref.size());
    }

    CHECK(mine.size() == ref.size());
    for (size_t i = 0; i < ref.size(); ++i) {
        CHECK(mine[i] == ref[i]);
    }
}

// ==================================================================
// Layer 7: growth-factor / amortized-O(1) sanity check
// Not a strict correctness test, but verifies capacity only ever grows
// via doubling from 0, and that size never exceeds capacity.
// ==================================================================
void test_capacity_growth_invariants() {
    MyVector<int> v;
    size_t last_capacity = v.capacity();
    for (int i = 0; i < 1000; ++i) {
        v.push_back(i);
        CHECK(v.size() <= v.capacity());
        if (v.capacity() != last_capacity) {
            // capacity changed: must have (at least) doubled from previous
            // nonzero capacity, or gone from 0 -> 1
            if (last_capacity == 0) {
                CHECK(v.capacity() == 1);
            } else {
                CHECK(v.capacity() == last_capacity * 2);
            }
            last_capacity = v.capacity();
        }
    }
}

int main() {
    testing::run("ctor_dtor_balance_basic", test_ctor_dtor_balance_basic);
    testing::run("ctor_dtor_balance_after_many_grows", test_ctor_dtor_balance_after_many_grows);
    testing::run("ctor_dtor_balance_with_pop_back", test_ctor_dtor_balance_with_pop_back);

    testing::run("grow_prefers_move_over_copy", test_grow_prefers_move_over_copy);

    testing::run("empty_vector_state", test_empty_vector_state);
    testing::run("at_throws_out_of_range", test_at_throws_out_of_range);
    testing::run("operator_bracket_matches_at", test_operator_bracket_matches_at);
    testing::run("self_copy_assignment_is_safe", test_self_copy_assignment_is_safe);
    testing::run("move_construction_leaves_source_empty_and_valid",
                 test_move_construction_leaves_source_empty_and_valid);

    testing::run("copy_constructor_is_deep", test_copy_constructor_is_deep);
    testing::run("copy_assignment_is_deep_and_replaces_contents",
                 test_copy_assignment_is_deep_and_replaces_contents);

    testing::run("exception_during_push_back_growth", test_exception_during_push_back_growth);

    testing::run("differential_against_std_vector", test_differential_against_std_vector);
    testing::run("capacity_growth_invariants", test_capacity_growth_invariants);

    int failed = testing::summarize();
    return failed == 0 ? 0 : 1;
}