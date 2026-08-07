// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_ERRORS_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_ERRORS_HPP

#include <atp/plugin_c.h>

namespace atp::bridge {

/// Turns the pending Python exception into the failure text of a lifecycle call.
///
/// The boundary is exception-free in both directions (plugin_c.h), and an exception left pending
/// would be neither: the next call into the interpreter would fail for a reason nobody could see.
/// This formats it the way the traceback module does — the script's own file and line included,
/// which is the whole value of doing it here rather than printing a bare message — hands the text to
/// set_error and clears the error.
///
/// Must be called with the GIL held and an error set.
/// @param api the host's callbacks
/// @param ctx the failing instance's context
/// @param where the entry point that failed, named first in the message
void report_error(const atp_api& api, atp_ctx* ctx, const char* where);

}  // namespace atp::bridge

#endif
