# A plugin project that is not part of the platform's build. It reaches the SDK only through
# find_package, which is the case the versioned ABI, the name-based type identity and the whole of
# cmake/Install.cmake exist for — and which nothing else in the repository exercises.
#
#     cmake -S templates/plugin -B <build> -DCMAKE_PREFIX_PATH=<where the SDK was installed>
#     cmake --build <build>
#
# Copy this directory somewhere else to start a plugin of your own; nothing here refers to the
# platform's source tree by path.
cmake_minimum_required(VERSION 4.1)
project(atp_template_plugin CXX)

find_package(AniToolsPlatform REQUIRED)

# The ABI this plugin was written against. Stating it turns a mismatch into a configure error here
# instead of a refused load inside the host. When the platform bumps plugin_abi, this line is meant
# to fail until the plugin has been looked at — that is the point of it, not an oversight.
atp_require_plugin_abi(10)

atp_add_plugin(atp_template_plugin SOURCES plugin.cpp)
