// Standalone runner of the procedural construction self test (no engine needed):
//     cmake --build --preset debug --target procedural_construct_tests

#include "procedural/construct/self_test.h"

#include <cstdio>



int main() {

    const GLT::construct::self_test_report report = GLT::construct::run_self_test();
    for (const std::string& f : report.failures)
        std::fprintf(stderr, "FAILED: %s\n", f.c_str());
    std::printf("construct self test: %u passed, %u failed (%.1f ms)\n", report.passed, report.failed, report.milliseconds);
    return report.failed == 0 ? 0 : 1;
}
