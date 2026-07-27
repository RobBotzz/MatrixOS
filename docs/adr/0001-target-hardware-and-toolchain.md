# ADR-0001: Pi Zero 2 W with 64-bit OS, aarch64 toolchain

- **Status:** Accepted
- **Date:** 2026-07-25

## Context

The project targets a Raspberry Pi Zero form factor for size and power reasons. Two
incompatible variants exist:

- **Pi Zero W** — BCM2835, single-core ARMv6, 32-bit only. Requires an
  `arm-linux-gnueabihf` toolchain.
- **Pi Zero 2 W** — BCM2710A1, 4x Cortex-A53 @ 1 GHz, 512 MB RAM, 64-bit capable.

CI already cross-compiles with `aarch64-linux-gnu-g++`, which only runs on the Zero 2 W with
a 64-bit OS. At the same time `pi-toolchain.cmake` declares `CMAKE_SYSTEM_PROCESSOR arm`,
and the `pi-zero-win` preset still references a 32-bit `arm-linux-gnueabihf` compiler with a
placeholder note. The repository currently claims both targets at once.

The choice also affects timing quality: rpi-rgb-led-matrix recommends isolating a CPU core
on multi-core Pis to avoid flicker, which is only possible on the quad-core Zero 2 W.

## Decision

Target the **Raspberry Pi Zero 2 W running 64-bit Raspberry Pi OS**. The only supported
build target is `aarch64`. 32-bit ARM is not supported.

## Consequences

- `pi-toolchain.cmake` must declare `CMAKE_SYSTEM_PROCESSOR aarch64` to match the compiler
  CI actually uses.
- The `pi-zero-win` preset is either repointed at a real aarch64 toolchain or removed;
  development now happens on WSL2, so removing it is the honest option.
- A CPU core can be isolated (`isolcpus`) for the panel's updater thread, which is the
  main lever for meeting NFR-5 (no visible flicker).
- 512 MB RAM is the memory ceiling; NFR-4 budgets 64 MB for MatrixOS.
- Four cores make an off-loop fetch thread for network apps (FR-27) realistic rather than
  a timing risk.
- Anyone with a Pi Zero W (v1) cannot run the binary. Accepted; this is a personal device
  project, not a distribution.

## Alternatives considered

- **Pi Zero W (32-bit)** — rejected: single core makes flicker-free rendering plus input
  plus later network work substantially harder, and it invalidates the existing CI setup.
- **Pi 4 / Pi 5** — rejected: ample performance and no cross-compilation pressure, but
  larger, hungrier, and overkill for driving one 64x32 panel. The constraint of a small
  device is part of the point.
- **Supporting both 32-bit and 64-bit** — rejected: doubles the build matrix and the
  testing surface for zero benefit at this stage.
