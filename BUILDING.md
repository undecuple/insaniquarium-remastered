# Building

Short version, on Linux or WSL with the 32-bit MinGW cross compiler:

```sh
tools/fetch-deps.sh     # downloads MinHook (checksum-checked) into third_party/
./build.sh              # the mod: build/dist/ (add --tests for the test-only mods and examples)
tools/package.sh        # the release zip and SHA256SUMS.txt in build/release/
tools/host-tests.sh     # native tests of the pure logic; needs no game, Wine or Windows
```

You should see `built build/dist/ddraw.dll` and one `built ...` line per mod, then `codec: ... 0 failed` from the host
tests. The same commit gives the same zip when built with the same tools: the releases are built by GitHub Actions in a
Debian container (`tools/ci-setup.sh` installs its tools).

Everything else (other systems, one mod only, testing in the game under Wine or Proton, writing a mod) is in
**[DEVELOPING.md](DEVELOPING.md)**.
