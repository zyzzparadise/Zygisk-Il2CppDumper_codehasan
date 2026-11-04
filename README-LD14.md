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


## Experimental Soul Knight ARM64 TLS diagnosis (LDPlayer 14 / Houdini)

This build includes an **opt-in, in-memory, version-specific** diagnostic override
for **ChillyRoom.RPC.RPCClient**. It is **OFF by default**. It does **not**
alter the APK or `libil2cpp.so` on disk. On Android/ARM64 it checks the original
20-byte code signatures before changing anything. Both targets must match.

The two functions and ARM64 RVAs are:

| Method | ELF RVA |
| --- | --- |
| `RPCClient.CheckErrorCodes` | `0x991F9F8` |
| `RPCClient.CheckCertHashTrusted` | `0x991FC98` |

`RGScript.Util.SslUtil` is **not patched**, because its callback appears to
log certificate errors before returning `true` on the inspected normal path.

### Enabling for a disposable LDPlayer instance

1. Back up the emulator and install the updated Zygisk module ZIP.
2. Confirm Magisk / Zygisk is active, and restart the emulator.
3. Run these commands from Windows CMD:

```cmd
adb shell su -c "setprop persist.il2cppdumper.package com.ChillyRoom.DungeonShooter"
adb shell su -c "setprop persist.il2cppdumper.tls_probe 1"
adb shell am force-stop com.ChillyRoom.DungeonShooter
adb logcat -b all -c
adb shell am start -n com.ChillyRoom.DungeonShooter/com.chillyroomsdk.sdkbridge.BasePlayerActivity
```

4. After launch, inspect only the module's diagnostic lines:

```cmd
adb shell su -c "logcat -b all -d -s zygisk_il2cppdumper:V" > tls-probe.log
```

A success indication is
`TLS_PROBE: applied two RPCClient trust-check overrides (in memory)`.
If you see `build mismatch`, `mprotect RW failed`, or another
`TLS_PROBE` error, the override was **not verified as applied**.
Even after success is logged, Houdini may cache translated instructions,
and network success must still be checked independently.

Disable the experiment and restart the game to restore original behavior:

```cmd
adb shell su -c "setprop persist.il2cppdumper.tls_probe 0"
adb shell am force-stop com.ChillyRoom.DungeonShooter
```

### Security and compatibility

- **Do not enable on a primary device/account.** When enabled, these
  modifications may cause the RPC client to accept untrusted certificates.
- This patch is specific to the analyzed ARM64 build only, not a general
  SSL pinning bypass; a game update should safely fail the byte check.
- It does not fix Android CA store issues, UnityWebRequest validation,
  PCAPdroid routing problems, or unrelated TLS stacks.
- Runtime behavior on LDPlayer 14 + Houdini has **not yet been tested**.


## LD14.3 diagnostics when Logcat is empty

If no lines with tag `zygisk_il2cppdumper` appear, the ARM64 worker now
writes an independent diagnostic file:

`/data/user/0/com.ChillyRoom.DungeonShooter/files/tls-probe-status.txt`

This status file is **overwritten when the ARM64 worker starts**, and
updated as each TLS-probe stage executes. It records whether
`persist.il2cppdumper.tls_probe` and
`persist.il2cppdumper.package` were readable in the translated ARM64 process,
plus detailed reasons for refusing to patch. It does not log certificates,
HTTP traffic, or secrets.

After installing the LD14.3 release ZIP, reboot the emulator and run:

```cmd
adb shell su -c "setprop persist.il2cppdumper.package com.ChillyRoom.DungeonShooter"
adb shell su -c "setprop persist.il2cppdumper.tls_probe 1"
adb shell am force-stop com.ChillyRoom.DungeonShooter
adb shell am start -n com.ChillyRoom.DungeonShooter/com.chillyroomsdk.sdkbridge.BasePlayerActivity
```

Wait for the main menu, then:

```cmd
adb shell su -c "cat /data/user/0/com.ChillyRoom.DungeonShooter/files/tls-probe-status.txt"
adb shell su -c "stat /data/user/0/com.ChillyRoom.DungeonShooter/files/dump.cs"
```

If the status file is absent despite a fresh dump, the ARM worker may not
have started or a different build of the module is running.
If the status file reports `read_count=0`, investigate the property view
inside the Houdini NativeBridge rather than changing the ELF RVA.
