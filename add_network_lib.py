Import("env")
import os

# ESP32 Arduino Core v3 splits WiFi into WiFi + Network libraries.
# PlatformIO's LDF can't automatically resolve this transitive dependency
# because the WiFi library.properties doesn't declare "Networking" as a dep.
# This script finds and adds the Network library to the build path.

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
network_lib = os.path.join(framework_dir, "libraries", "Network", "src")

if os.path.isdir(network_lib):
    env.Append(CPPPATH=[network_lib])
    print(">> Added Network library include path: %s" % network_lib)

    # Also add source files for compilation
    env.BuildSources(
        os.path.join("$BUILD_DIR", "NetworkLib"),
        network_lib,
        src_filter=["+<*.cpp>"]
    )
    print(">> Added Network library source files")
else:
    print("!! Network library not found at: %s" % network_lib)
