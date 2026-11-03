# Third-party dependencies

Dependencies are managed by the root vcpkg.json and vcpkg-configuration.json.
MicroTeX uses the overlay port in ports/microtex; tinyxml2, qtbase and qtsvg use
registry ports. No third-party source checkout is required.

The former local source trees (including local modifications) were preserved in
build/legacy-third-party during migration and are not used by the build.
