# eaux / rust

Rust crates, laid out as `rust/{topic}/{crate}/`. Each crate has its own
README, Makefile and MIT license. `make` here (or at the repo root) builds
and tests every crate.

## Releasing to crates.io

- Publish only after the `rust` GitHub Actions workflow passes on every OS
  in the matrix (ubuntu, macos, windows) and the MSRV job. Passing locally
  on one OS is not enough; Windows-only bugs have shipped green on Linux.
- Publish from the exact commit CI passed on, with a clean tree.
- While a crate's design is unproven, version it `0.0.1-alpha.n` and bump
  `n` for each release. Consumers pin exactly (`crate-name = "=0.0.1-alpha.n"`).
  `0.1.0` is the first release meant for general use.
