# CloudGTA managed Remote Play host

This fork adds one narrow process adapter around chiaki-ng's existing direct
`RunStream` path. It does not change transport, decoder, renderer, audio or
controller code.

The distributed target is compiled as `managed-host-only`: the normal chiaki
UI, registration flow and legacy secret-bearing CLI options are not part of
the executable's accepted command line.

The executable accepts managed input only with
`--cloudgta-host-protocol=1`. The credential frame is the base64 encoding of
the fixed binary value `version(1) || regist_key(16) || morning(16)`. It is
read only from framed stdin, never from arguments, environment variables,
temporary files or Qt settings. Input credential buffers are explicitly
zeroed after the libchiaki session is created.

The closed launch-frame allowlist currently accepts only the pilot profile:
PS5, H264, 1280x720, 60 FPS, 2-15 Mbps, OpenGL and an allowlisted FFmpeg
hardware-decoder choice. Unknown fields and non-private console targets fail
closed.

Run `cloudgta-remoteplay-host.exe --cloudgta-host-self-test` after deployment.
Successful output is one `{"event":"stopped"}` line and exit code 0.
