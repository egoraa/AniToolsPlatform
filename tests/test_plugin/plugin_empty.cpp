#include <atp/plugin.hpp>

// A library without the atp contract: the loader has to report the missing atp_abi_version. One
// unrelated export guarantees that MSVC generates a DLL at all.
ATP_PLUGIN_EXPORT void unrelated_symbol() {}
