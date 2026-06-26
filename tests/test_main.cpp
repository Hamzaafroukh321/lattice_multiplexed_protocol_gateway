#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

}  // namespace

void register_frame_tests();
void register_channel_tests();
void register_flow_tests();
void register_plugin_tests();
void register_replay_tests();
void register_scheduler_tests();
void register_trace_tests();
void register_handshake_tests();
void register_multiplex_tests();

void add_test(const char* name, TestFn fn) {
  registry().push_back(TestCase{name, fn});
}

int main() {
  register_frame_tests();
  register_channel_tests();
  register_flow_tests();
  register_plugin_tests();
  register_replay_tests();
  register_scheduler_tests();
  register_trace_tests();
  register_handshake_tests();
  register_multiplex_tests();

  int failures = 0;
  for (const TestCase& test : registry()) {
    try {
      test.fn();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
    }
  }
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  return 0;
}
