# iCUE Battery Helper

A simple utility that displays a tray icon with the battery percentage of all currently connected iCUE devices.

Uses the official iCUE SDK.

## Building

### Needed software

- C++ toolchain from Visual Studio
- [xmake](https://xmake.io/#/)

### Getting the iCUE SDK

The iCUE SDK has no license information that I could find, so I am not gonna risk it and thus this repository doesn't contain the iCUE SDK files.

To get them yourself, download the SDK ZIP from the [official Releases](https://github.com/CorsairOfficial/cue-sdk/releases) and copy the contents of the `iCUESDK` folder inside the ZIP into
the `deps/cue-sdk` folder. The resulting layout should have the `doc`, `include`, `lib` and `redist` folder inside `deps/cue-sdk`.

### Building with xmake

To build the project using xmake, simply run `xmake build` inside the root of the repository. This will create a binary you can run.

> If there are any compilation errors e.g. due to failed linking, make sure the iCUE SDK has been properly placed.
