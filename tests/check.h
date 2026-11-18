// A minimal test harness: TEST(name) { CHECK(...); } and one main in test_main.cpp.
#pragma once
#include <cstdio>

struct TestCase {
    const char* name;
    void (*fn)();
    TestCase* next;
};

inline TestCase*& test_list() {
    static TestCase* head = nullptr;
    return head;
}

inline int& test_failures() {
    static int n = 0;
    return n;
}

struct TestRegistrar {
    TestRegistrar(TestCase* t) {
        t->next = test_list();
        test_list() = t;
    }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static TestCase case_##name = {#name, test_##name, nullptr}; \
    static TestRegistrar registrar_##name(&case_##name);   \
    static void test_##name()

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            test_failures()++;                                           \
        }                                                                \
    } while (0)
