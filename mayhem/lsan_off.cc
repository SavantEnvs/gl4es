// Leak detection off for every fuzz and standalone binary (SPEC §6.2 item 15): gl4es keeps
// its global GL state for the life of the process, which is not the bug class fuzzed here.
extern "C" int __lsan_is_turned_off() { return 1; }
