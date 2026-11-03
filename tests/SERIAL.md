# Serial connection verification

Run the integration test without attaching a device:

```sh
cmake --build build --target banchoxterm_tests
QT_QPA_PLATFORM=offscreen build/banchoxterm_tests testSerialConnectionThroughVirtualPort -v1
```

On Linux the test creates a fresh PTY with `posix_openpt`. BanchoXterm opens the
slave through its actual `QSerialPort` and `TerminalTab` path; the master acts as
the device. Neither socat nor a kernel module is required. The test checks:

- Opening at 9600 and 115200 with 8N1, and configuring hardware flow control.
- Keeping the connection usable when DTR/RTS operations are unsupported.
- Keyboard transmission and the configured Enter/Backspace sequences.
- Sending 64 KiB of binary data, preserving all byte values and their order.
- Receiving fragmented UTF-8, including a split multibyte character, and rendering it.
- Marking the session disconnected when the peer disappears.

A PTY does not simulate electrical signals, timing at the selected baud rate,
parity errors, physical RTS/CTS handshaking, or a specific USB adapter's driver.
Those require a physical device or adapter. XMODEM/YMODEM/ZMODEM transfers are
outside this test. On Windows this PTY test is explicitly skipped; end-to-end
Windows verification requires a virtual COM pair or a real serial connection.

## GitHub CI before considering an upload verified

After pushing changes, inspect the CI run for that exact commit and wait for the
required checks to finish successfully. A successful local build is insufficient.
The current workflow runs formatting and Linux build/tests on pushes to `main`
and on PRs targeting `main`. Windows x64 and ARM64 jobs run only for version tags
(`v*`), so a normal push or PR does not validate Windows. When a version is
published, verify both Windows matrix jobs as well as the Linux and formatting jobs.
