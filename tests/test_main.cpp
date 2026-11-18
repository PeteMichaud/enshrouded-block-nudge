#include "check.h"

int main() {
    int count = 0;
    for (TestCase* t = test_list(); t; t = t->next) {
        const int before = test_failures();
        t->fn();
        printf("%s %s\n", test_failures() == before ? "ok  " : "FAIL", t->name);
        count++;
    }
    printf("%d tests, %d failed checks\n", count, test_failures());
    return test_failures() ? 1 : 0;
}
