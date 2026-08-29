// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_HPP
#define ATP_RUNTIME_HPP

/// @file
/// Umbrella header of the host runtime: the composite, the pipeline and its runner, the plugin
/// loader, the log ring and pump, and the config subsystem. A new runtime header is declared here.
///
/// Two headers are deliberately absent, and those absences are the thing to know about this file.
///
/// runtime/socket_platform.hpp exists to include <winsock2.h> on Windows, and a header that reaches
/// every runtime consumer must not drag the socket stack — nor its macros, nor its rule that it has to
/// precede <windows.h> — into a translation unit that only wanted a pipeline. The three files that
/// open a socket include it by name.
///
/// runtime/config_value_json.hpp is absent for the same class of reason. It converts between a
/// config::node and a document library's node, so it names that library, and a header reaching every
/// runtime consumer must not — which is the whole reason json_codec.hpp has a .cpp behind it instead of
/// an inline body. The runtime itself no longer uses it at all; the callers that speak the protocol,
/// MCP and studio's client, include it by name and link the library themselves.

#include <atp/runtime/c_config.hpp>
#include <atp/runtime/c_module.hpp>
#include <atp/runtime/command_queue.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/config_file.hpp>
#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_path.hpp>
#include <atp/runtime/config_source.hpp>
#include <atp/runtime/config_tree_source.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/connection_sample.hpp>
#include <atp/runtime/console_encoding.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/host_node.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/runtime/log_pump.hpp>
#include <atp/runtime/log_ring.hpp>
#include <atp/runtime/module_loader.hpp>
#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/pipeline_runner.hpp>
#include <atp/runtime/property_override.hpp>
#include <atp/runtime/raw_config.hpp>
#include <atp/runtime/thread_name.hpp>
#include <atp/runtime/utf8_path.hpp>

#endif
