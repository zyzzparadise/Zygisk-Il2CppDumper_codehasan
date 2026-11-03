# Soul Knight TLS disassembly helper

This *optional local tool* extracts ARM64 disassembly for the two certificate-verification implementations found in the matching \`dump.cs\`. It **does not modify the APK** or send the game binary to GitHub.

## Windows, easiest approach

1. Download the repository ZIP and extract it.
2. Drag **your own** \`libil2cpp.so\` onto \`tools/SoulKnight-TLS-disasm.cmd\`.
3. Wait for the script to install the Capstone Python package (if needed) and print the output location.
4. Open/upload \`tools/SoulKnight-TLS-disasm.txt\` (small text file).

Optional: put \`dump.cs\` next to your \`libil2cpp.so\` to include known method names on matching branch destinations.

### Addresses analyzed

- \`RPCClient.CheckErrorCodes\` 0x991F9F8
- \`RPCClient.CheckCertHashTrusted\` 0x991FC98
- \`RPCClient.cctor\` 0x991F32C (trusted-root hash set)
- \`RPCClient callback\` 0x99225B8
- \`SslUtil.CheckErrorCodes\` 0x7D16B00
- \`SslUtil.CheckCertHashTrusted\` 0x7D16DCC
- \`SslUtil.cctor\` 0x7D167F8 (trusted-root hash set)
- \`SslUtil callback\` 0x7D17140

It also includes the callback installation functions. The script translates RVAs through the ELF PT_LOAD program headers rather than assuming virtual addresses equal file offsets.

The method addresses are tied to the *specific* Soul Knight binary and dumper output supplied by the user. A different game version can have different addresses.

This script does not patch pinning or guarantee any certificate-bypass behavior.
