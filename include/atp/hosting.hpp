// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_HPP
#define ANITOOLSPLATFORM_HOSTING_HPP

/// @file
/// Umbrella header of what a host and a loader need on top of the module author's SDK: the factories,
/// the registry, the registrar and the null host. A new header goes here rather than into module.hpp
/// when a module author has no use for it.

#include <atp/hosting/module_declaration.hpp>
#include <atp/hosting/module_factory.hpp>
#include <atp/hosting/module_factory_base.hpp>
#include <atp/hosting/module_registrar.hpp>
#include <atp/hosting/module_registry.hpp>
#include <atp/hosting/null_host.hpp>
#include <atp/hosting/registration_api.hpp>
#include <atp/module.hpp>

#endif
