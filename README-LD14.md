# Zygisk-Il2CppDumper — LDPlayer 14 experimental patch

Based on [codehasan/Zygisk-Il2CppDumper v1.6.1](https://github.com/codehasan/Zygisk-Il2CppDumper).

## Why this fork exists

On LDPlayer 14 (Android 14, x86_64 + Houdini), an ARM64 Unity game may show
`libil2cpp.so` in `/proc/<pid>/maps` while the original dumper reports
`dlopen libart.so failed`, followed by `libil2cpp.so not found`.

This fork obtains `JavaVM` directly from the Zygisk-provided `JNIEnv` instead
of calling `dlopen("libart.so")`, and passes it to the NativeBridge worker.
It also adds diagnostic messages for the NativeBridge load path.

This is an **experimental** compatibility fix. It has not been validated on a
live LDPlayer 14 instance. The ARM64 NativeBridge stage might still fail.

## GitHub Actions build

Go to [Actions → Build](https://github.com/zyzzparadise/Zygisk-Il2CppDumper_codehasan/actions/workflows/build.yml)
and run the workflow, or push to `master`.

The workflow installs JDK 17, Android platform 36, NDK 28.2.13676358
and CMake 4.1.2; then runs `./gradlew :module:zipRelease`.
A successful run creates an artifact named `Zygisk-Il2CppDumper-LD14`.
Download and **unzip the GitHub artifact**; install the inner
`*-release.zip` in Magisk.

## Target game

```sh
adb shell su -c "setprop persist.il2cppdumper.package com.ChillyRoom.DungeonShooter"
adb shell am force-stop com.ChillyRoom.DungeonShooter
adb shell am start -n com.ChillyRoom.DungeonShooter/com.chillyroomsdk.sdkbridge.BasePlayerActivity
```

After the game initializes, look for:

```text
/data/user/0/com.ChillyRoom.DungeonShooter/files/dump.cs
```

Diagnostics:

```sh
adb shell su -c "logcat -b all -d -v threadtime -s zygisk_il2cppdumper:V"
```

Because `libil2cpp.so` is ARM64 and Houdini runs in an x86_64 host process,
a successful build **does not guarantee** a successful in-game dump. Do not
disable other security controls or install this on your primary emulator before
testing in a backup/snapshot.
