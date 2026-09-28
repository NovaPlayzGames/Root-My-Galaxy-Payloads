# e3q-S928USQS6DZH3 artifacts

This directory contains the release payload for Galaxy S24 Ultra SM-S928U1 build
`S928USQS6DZH3` and kernel
`6.1.145-android14-11-33419968-abS928USQS6DZH3`.

## Files

- `cve-2026-43499-app.so` - Release app payload (104128 bytes)

## Build Information

Built from target profile: `src/targets/e3q-S928USQS6DZH3/`

Build fingerprint: `samsung/e3qsqw/e3q:16/BP4A.251205.006/S928USQS6DZH3:user/release-keys`

## Status

This build is based on the DZF2 profile but configured for DZH3 firmware. It
has not been hardware-tested. The offsets and configuration are derived from
the DZH3 kernel analysis.

## KernelSU

The KernelSU late-load artifact in this directory (`ksud-e3q-S928USQS6DZH3-kdp`)
is a self-contained loader, not an upstream `ksud` binary. It embeds the
no-patch-text KernelSU module
(`kernelsu/android14-6.1_kernelsu-e3q-S928BXXS6DZF2-kdp.ko`, vermagic patched to
`abS928USQS6DZH3`) and loads it via kallsyms symbol pre-binding +
`finit_module`, ignoring the helper's `late-load` argv.

Rationale: the previous DZF2-renamed `ksud` embedded a live-text-patching
module which panics under Samsung EL2/RKP on this target. The no-patch-text
module loads cleanly and KernelSU 3.2.5 control verified on hardware
(uid=2000 shell, `version=32525 flags=0x5`).

Source: `../../repo/ksuload.c` (loader) — see commit history.

## Compatibility

- Model: SM-S928U1 (Galaxy S24 Ultra)
- Firmware: S928USQS6DZH3
- Kernel: 6.1.145-android14-11-33419968-abS928USQS6DZH3

## Notes

This is a DZH3-specific build. Do not use this payload for other firmware
builds without verification.