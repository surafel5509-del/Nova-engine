# Toolchain setup (reproducible)

Everything installs from official sources into the user home (plus apt JDK).

| Component    | Version          | Source |
|--------------|------------------|--------|
| JDK          | OpenJDK 21       | `apt install openjdk-21-jdk-headless` (Debian 13) |
| Android SDK  | cmdline-tools 12.0 | https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip |
| Platform     | android-34       | sdkmanager `platforms;android-34` |
| Build tools  | 34.0.0           | sdkmanager `build-tools;34.0.0` |
| NDK          | 26.1.10909125    | sdkmanager `ndk;26.1.10909125` |
| CMake        | 3.22.1           | sdkmanager `cmake;3.22.1` |
| Gradle       | 8.9 (wrapper)    | https://services.gradle.org/distributions/gradle-8.9-bin.zip |
| AGP          | 8.5.2            | google() |
| Kotlin       | 2.0.21 (+ compose compiler plugin) | gradlePluginPortal() |
| Compose BOM  | 2024.09.02       | google() |
| kotlinx-serialization-json | 1.7.3 | mavenCentral() |
| nlohmann/json | 3.11.3 (vendored, MIT) | https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp |

Pinned build settings: `minSdk 26`, `target/compileSdk 34`, ABIs
`arm64-v8a` + `x86_64`, C++17, `c++_shared` STL, GL ES 3.0 feature declared
(3.x API subset; the engine requests a 3.x context and uses `#version 300 es`
shaders — a strict ES 3.2 / Vulkan path is planned with Phase 6).

## Steps

```bash
sudo apt-get install -y openjdk-21-jdk-headless unzip zip build-essential

export ANDROID_HOME=$HOME/android-sdk
mkdir -p $ANDROID_HOME/cmdline-tools
curl -sSL -o /tmp/cmdtools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip -q /tmp/cmdtools.zip -d $ANDROID_HOME/cmdline-tools
mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest

yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager \
  "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
  "cmake;3.22.1" "ndk;26.1.10909125"

# local.properties (git-ignored): sdk.dir=$HOME/android-sdk
```
