#ifndef MEM_LOG_H
#define MEM_LOG_H

// WARNING: This heap memory logger is for debugging purposes only.
// It is not thread-safe, it intentionally leaks memory, and may cause cancer.

#include <iosfwd>

// Each call to operator new is tagged with a string identifying the source
// filename, line, and symbol of the nearest caller in the stack trace whose
// filename has the case-sensitive prefix MEM_LOG_PATH.
#ifndef MEM_LOG_PATH
#define MEM_LOG_PATH "c:\\users"
#endif

namespace mem
{
    // Print the current log of heap usage to the given stream;
    // print_log(cout) is called automatically on program exit.
    void print_log(std::ostream& os);
} // namespace mem

#endif // MEM_LOG_H
