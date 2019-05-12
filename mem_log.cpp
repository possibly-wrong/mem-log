#include "mem_log.h"
#include <new>
#include <cstdlib>
#include <map>
#include <string>
#include <iostream>
#include <iomanip>

// Windows-specific
#include <Windows.h>
#pragma warning(push)
#pragma warning(disable: 4091)
#include <DbgHelp.h>
#pragma warning(pop)
#pragma comment(lib, "DbgHelp.lib")
#include <sstream>

namespace // unnamed namespace
{
    bool enabled = true;

    // Make a best effort to automate printing the "final" heap usage log,
    // although some later static object destructors may still call new/delete.
    struct OnExit
    {
        ~OnExit()
        {
            mem::print_log(std::cout);
        }
    } on_exit;

    // A Block records a single call to new, and is destroyed on delete.
    struct Block
    {
        Block(std::size_t bytes, const std::string& caller) :
            bytes(bytes), caller(caller) {}
        std::size_t bytes;
        std::string caller;
    };

    // A Caller records heap usage from a single source code line and symbol.
    struct Caller
    {
        Caller() :
            heap(), freed(), max_alloc() {}
        struct Count
        {
            Count() : blocks(0), bytes(0) {}
            std::size_t blocks, bytes;
        } heap, freed, max_alloc;
    };

    // The Logger records all currently allocated Blocks, and the history of
    // memory usage by all Callers.
    struct Logger
    {
        Logger() :
            heap(), log()
        {
            // Windows-specific
            process = GetCurrentProcess();
            SymSetOptions(SymGetOptions() |
                SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            SymInitialize(process, NULL, TRUE);
            symbol = (PSYMBOL_INFO)buffer;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        }
        std::map<void *, Block> heap;
        std::map<std::string, Caller> log;

        // Windows-specific
        HANDLE process;
        void *stack[65535];
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol;
        IMAGEHLP_LINE64 line;
    };

    // Construct the Logger on first call to new, allocated on the heap and
    // never destroyed, since we can't guarantee lifetime beyond the last call
    // to new/delete.
    Logger& get_logger()
    {
        static Logger* logger = new Logger();
        return *logger;
    }

    // Return true iff string s begins with the given prefix.
    bool starts_with(const char *s, const char *prefix)
    {
        while (*prefix)
        {
            if (*prefix++ != *s++)
            {
                return false;
            }
        }
        return true;
    }
} // unnamed namespace

void mem::print_log(std::ostream& os)
{
    // Temporarily disable logging while accessing/creating logger.
    bool enable = enabled;
    enabled = false;
    os << "                    Heap            Freed      Max. Alloc.\n";
    os << "==========================================================\n";
    for (auto&& i : get_logger().log)
    {
        const int w = 16; // column width
        os << "Caller: " << i.first << "\nBlocks: " <<
            std::setw(w) << std::right << i.second.heap.blocks << " " <<
            std::setw(w) << std::right << i.second.freed.blocks << " " <<
            std::setw(w) << std::right << i.second.max_alloc.blocks <<
            "\nBytes:  " <<
            std::setw(w) << std::right << i.second.heap.bytes << " " <<
            std::setw(w) << std::right << i.second.freed.bytes << " " <<
            std::setw(w) << std::right << i.second.max_alloc.bytes <<
            std::endl;
    }
    enabled = enable;
}

void* operator new(std::size_t count)
{
    void *ptr = std::malloc(count);
    if (ptr == 0)
    {
        throw std::bad_alloc();
    }
    if (enabled)
    {
        // Temporarily disable logging while accessing/creating logger.
        enabled = false;
        {
            // Walk the stack searching for caller in MEM_LOG_PATH.
            Logger& logger = get_logger();
            std::string caller = "NOT_FOUND";

            // Windows-specific
            USHORT frames = CaptureStackBackTrace(0, 65535, logger.stack,
                NULL);

            // Start at frame 1 instead of 0 to skip this function (new).
            for (USHORT i = 1; i < frames; ++i)
            {
                DWORD64 address = (DWORD64)(logger.stack[i]);
                DWORD64 d64 = 0;
                DWORD d = 0;
                if (SymFromAddr(logger.process, address, &d64,
                        logger.symbol) &&
                    SymGetLineFromAddr64(logger.process, address, &d,
                        &logger.line) &&
                    starts_with(logger.line.FileName, MEM_LOG_PATH))
                {
                    std::ostringstream oss;
                    oss << logger.line.FileName << "(" <<
                        logger.line.LineNumber << "):" << logger.symbol->Name;
                    caller = oss.str();
                    break;
                }
            }

            // Check for duplicate pointer (this should never happen).
            // assert(logger.heap.find(ptr) == logger.heap.end());
            logger.heap.emplace(ptr, Block(count, caller));
            Caller& usage = logger.log[caller];
            ++usage.heap.blocks;
            usage.heap.bytes += count;
            if (usage.heap.bytes > usage.max_alloc.bytes)
            {
                usage.max_alloc = usage.heap;
            }
        }
        enabled = true;
    }
    return ptr;
}

void operator delete(void *ptr)
{
    if (enabled)
    {
        // Temporarily disable logging while accessing/creating logger.
        enabled = false;
        {
            Logger& logger = get_logger();
            auto&& i = logger.heap.find(ptr);

            // Check for unknown pointer (this should never happen).
            // assert(i != logger.heap.end());
            Caller& usage = logger.log[i->second.caller];
            --usage.heap.blocks;
            usage.heap.bytes -= i->second.bytes;
            ++usage.freed.blocks;
            usage.freed.bytes += i->second.bytes;
            logger.heap.erase(i);
        }
        enabled = true;
    }
    std::free(ptr);
}
