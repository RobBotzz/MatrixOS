# Architecture Decision Records

Short records of decisions that had a plausible alternative. The point is not ceremony —
it is that in six months the question "why is it like this?" has an answer, and that a
decision consciously left open is visibly different from one that was never considered.

## Index

| # | Decision | Status |
| --- | --- | --- |
| [0001](0001-target-hardware-and-toolchain.md) | Pi Zero 2 W with 64-bit OS, aarch64 toolchain | Accepted |
| [0002](0002-display-abstraction-and-simulator.md) | Display abstraction with a simulator backend from day one | Accepted |
| [0003](0003-single-process-app-model.md) | One process, one active app, cooperative tick | Accepted |
| [0004](0004-network-app-runtime.md) | How network apps get their data | **Deferred** |
| [0005](0005-deployment-model.md) | Versioned releases with a systemd service | Proposed |
| [0006](0006-single-encoder-input.md) | A single rotary encoder is the only control | **Superseded by 0009** |
| [0007](0007-appliance-provisioning.md) | Appliance provisioning — golden image, captive portal, setup as an app | Accepted |
| [0008](0008-power-loss-resilience.md) | Power-loss resilience and flash longevity | Accepted |
| [0009](0009-dedicated-home-button.md) | A dedicated home button alongside the encoder | Accepted |
| [0010](0010-own-encoder-decoding.md) | Decode the encoder ourselves rather than using the kernel driver | Accepted |
| [0011](0011-state-store-format.md) | One key-value file per namespace for persisted state | Accepted |
| [0012](0012-own-http-server.md) | An HTTP server of our own | Accepted |
| [0013](0013-wifi-provisioning-via-networkmanager.md) | WiFi provisioning through NetworkManager, driven by `nmcli` | Accepted |
| [0014](0014-config-page-in-the-binary.md) | Two web pages, built differently, both compiled into the binary | Accepted |
| [0015](0015-time-provider-and-unknown-time.md) | A time provider, and an explicit "time unknown" state | Accepted |

## Conventions

- One file per decision, numbered sequentially, never renumbered.
- Status is one of `Proposed`, `Accepted`, `Deferred`, `Superseded by ADR-NNNN`.
- A record is never edited to change its decision. It gets superseded by a new one, so the
  history of reasoning stays readable.
- A `Deferred` record must name its **trigger** — the concrete event that forces the
  decision — and the criteria that will decide it. Deferred is a plan, not a shrug.

## Template

```markdown
# ADR-NNNN: Title

- **Status:** Proposed | Accepted | Deferred | Superseded by ADR-NNNN
- **Date:** YYYY-MM-DD

## Context
What forces the decision. Facts and constraints, not opinions.

## Decision
What was decided, stated in one or two sentences.

## Consequences
What this makes easy, what it makes hard, what it obliges us to do.

## Alternatives considered
Each with the reason it lost. An alternative with no stated reason means the decision
was not actually made.
```
