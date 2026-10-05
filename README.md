# Lunar

Small experimental kernel. (It's really stupid)

Currently only supports x86_64 and riscv64 (rv64gc) but may support other architectures in the future.

Lunar is booted via [Theia](https://github.com/vcvtph2ps/theia), its prekernel, and is built as part of [Terra](https://github.com/vcvtph2ps/terra) (the system distribution)

## Licensing

Lunar itself is licensed under BSD 3-Clause (see `LICENSE`). It statically links
third-party components fetched at build time; their notices and license texts
are in [`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES) and `LICENSES/`, and must be
included with source and binary distributions.
