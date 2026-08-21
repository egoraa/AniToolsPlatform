// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONSOLE_ENCODING_HPP
#define ATP_RUNTIME_CONSOLE_ENCODING_HPP

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace atp::runtime {

/// Makes a Windows console show the UTF-8 a host prints, and puts it back on the way out.
///
/// The tree compiles with UTF-8 as the execution charset, so every literal a host prints is UTF-8 —
/// including the em dashes in its own messages. A Windows console decodes what it is given with its
/// output code page, which is 866 or 1251 on a typical Russian machine and never UTF-8 by default, so
/// those bytes arrive as mojibake. This costs one call to fix.
///
/// It restores the previous page in the destructor, and that is not politeness for its own sake: the
/// code page belongs to the console window and outlives the process, so a host that changed it and
/// walked away would leave every later program in that window decoding through UTF-8. A process
/// killed outright cannot restore it — there is no handler that would run — which is a known and
/// accepted gap rather than something to solve with a signal handler.
///
/// A process with no console (a service, a redirected pipe, a GUI child) gets nothing done to it:
/// GetConsoleOutputCP answers zero, and a redirected stream carries the bytes through untouched
/// whatever the console page says. That is why `atp_mcp` may use this freely — its protocol stream is
/// a pipe in every real deployment, and the only thing this changes is whether a person debugging it
/// in a console can read the log lines on stderr.
///
/// Nothing happens off Windows: a modern POSIX terminal is UTF-8 already.
class console_utf8 {
   public:
    console_utf8() {
#if defined(_WIN32)
        const UINT current = GetConsoleOutputCP();
        if (current != 0 && current != CP_UTF8 && SetConsoleOutputCP(CP_UTF8) != 0) {
            previous_ = current;
        }
#endif
    }

    ~console_utf8() {
#if defined(_WIN32)
        if (previous_ != 0) {
            (void)SetConsoleOutputCP(previous_);
        }
#endif
    }

    console_utf8(const console_utf8&) = delete;
    console_utf8& operator=(const console_utf8&) = delete;
    console_utf8(console_utf8&&) = delete;
    console_utf8& operator=(console_utf8&&) = delete;

   private:
#if defined(_WIN32)
    UINT previous_ = 0;
#endif
};

}  // namespace atp::runtime

#endif
