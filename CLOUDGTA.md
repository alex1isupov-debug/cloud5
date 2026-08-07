# CloudGTA Player

CloudGTA Player is the Windows lab client for the CloudGTA MVP. It is a fork
of [chiaki-ng](https://github.com/streetpea/chiaki-ng) version 1.10.0 at commit
`0c4a45df0cae2af2ba2daef84e881850b07038a3`.

## Current lab scope

- Windows x64 portable build.
- Local-network PS5 discovery, registration, wake-up and Remote Play.
- An isolated `cloudgta-lab` settings profile.
- CloudGTA application, executable and Windows resource branding.

The current lab build intentionally keeps the proven chiaki-ng streaming
engine and registration UI. Control-plane device-code and session-ticket
integration is the next layer and is not represented as complete here.

## Licensing and attribution

This fork remains licensed under GNU AGPLv3. The original chiaki-ng and Chiaki
copyright notices and source history are retained. See `LICENSE` and the About
dialog bundled with the application.

## Windows build

Run the build scripts from an MSYS2 `MINGW64` shell after installing the
dependencies used by `.github/workflows/build-msys2.yml`:

```bash
scripts/cloudgta/build-windows-msys2.sh
scripts/cloudgta/package-windows-msys2.sh
```

The portable ZIP is written to `dist/cloudgta-player-lab-win64.zip`.
