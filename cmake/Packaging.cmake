# CPack configuration: turns the install tree into something that can be handed to a person.
#
# Included last, after every install() rule in the project, because include(CPack) freezes the
# CPACK_* variables at the point it runs and generates CPackConfig.cmake from them.
#
# The install rules do name components — `sdk` and `applications` — but the package deliberately stays
# one archive. The components exist so that a build can install a part of the tree (`cmake --install
# --component sdk` is how the out-of-tree plugin job gets the headers without building every
# executable); splitting the delivery as well would produce several archives per platform and a
# release page listing them, in exchange for saving a reader a few megabytes of headers they can
# ignore. CPack packages monolithically unless asked otherwise, so naming components costs nothing
# here — but do not switch on CPACK_ARCHIVE_COMPONENT_INSTALL without deciding that question.

set(CPACK_PACKAGE_NAME "AniToolsPlatform")
set(CPACK_PACKAGE_VENDOR "AniTools")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular pipeline platform: the studio, the hosts and the plugin SDK")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "AniToolsPlatform")
set(CPACK_PACKAGE_FILE_NAME "AniToolsPlatform-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# Quoting is left to CPack rather than done by hand — a vendor or a path with a space in it otherwise
# reaches the generated config unquoted and breaks it there, where the error makes no sense.
set(CPACK_VERBATIM_VARIABLES ON)

# An archive on Windows and Linux, a disk image on macOS. NSIS would give Windows a real installer and
# is the obvious next step, but it needs makensis on whatever machine builds the release, whereas a
# ZIP needs nothing and unpacks where the user wants — the studio already runs from any directory,
# since its Qt runtime travels beside it.
if (WIN32)
    set(CPACK_GENERATOR ZIP)
elseif (APPLE)
    set(CPACK_GENERATOR DragNDrop)
else ()
    set(CPACK_GENERATOR TGZ)
endif ()

include(CPack)
