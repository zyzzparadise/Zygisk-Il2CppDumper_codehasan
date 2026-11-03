# Zygisk-Il2CppDumper — LDPlayer 14 experimental patch

Based on codehasan/Zygisk-Il2CppDumper tag v1.6.1.

Changes:
- Gets JavaVM from Zygisk instead of dlopen("libart.so").
- Passes JavaVM to the NativeBridge initialization worker.
- Waits for ActivityThread.currentApplication().
- Adds NativeBridge failure diagnostics.
- Adds a GitHub Actions Build workflow.
- Uses a separate patched module version.

Build:
GitHub -> Actions -> Build -> Run workflow.

Output:
Download the artifact, extract it, then install the inner
Magisk module ZIP.

Target:
adb shell su -c "setprop persist.il2cppdumper.package com.ChillyRoom.DungeonShooter"

Expected output:
/data/data/com.ChillyRoom.DungeonShooter/files/dump.cs

Log tag:
zygisk_il2cppdumper

NOTE:
This patch has not been runtime-tested on LDPlayer 14.
Additional NativeBridge compatibility changes may be needed.
