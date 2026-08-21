include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/external/downloads
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/nlohmann_json-src
        SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

# The one compiled unit that names a document library. It is STATIC and nlohmann is PRIVATE to it, so
# atp_runtime's consumers get json_parse/json_dump without getting the library, its headers or its
# include path — which is what lets <atp/runtime.hpp> stop costing 25k lines of it per translation unit.
# It is also the template for the rest of the runtime whenever that follows it out of the headers.
add_library(atp_json STATIC json_codec.cpp)
target_include_directories(atp_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atp_json
        PUBLIC atp_platform
        PRIVATE nlohmann_json::nlohmann_json atp_warnings)

add_library(atp_runtime INTERFACE)
target_include_directories(atp_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ws2_32 comes with the runtime because socket_platform.hpp lives here: the MCP server, the studio's
# client and the tests all open sockets, and all three link atp_runtime already. Putting it on the
# MCP library instead would leave the studio — which must not depend on the MCP layer — without it.
target_link_libraries(atp_runtime INTERFACE atp_platform atp_json
        $<$<PLATFORM_ID:Windows>:ws2_32>)

# /bigobj is a technical consequence of a header that lives here, not a policy, which is why it rides on
# the runtime target rather than on atp_warnings: module_loader.hpp includes c_module.hpp, that builds a
# port from a runtime atp_kind and therefore instantiates input<T>, queued_input<T>, output<T> and
# property<T> for every kind, and each output instantiation drags in the std::any boxing of
# io::input_base::erased_of<T>(). MSVC emits those COMDATs into every unit that includes the loader
# whether it calls anything or not — measured, an object file goes from 25 sections to 488 — and both
# atp_tests and src/mcp/main.cpp crossed the 65,279-section limit of the default object format. The flag
# only raises that limit: no code changes, nothing costs more at run time, the linker folds the duplicates
# anyway. It reaches exactly the targets that include the loader, and no further — atp_runtime is
# deliberately not exported, so no plugin author's build inherits it. gcc and clang have no such limit.
if (MSVC)
    target_compile_options(atp_runtime INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif ()
