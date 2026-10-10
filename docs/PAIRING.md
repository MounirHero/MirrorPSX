# AirPlay PIN pairing and Windows DPI

The client initiates PIN pairing via an AirPlay RTSP request. UxPlay's
`display_pin` callback delivers the **actual** generated four-digit code; the
UI does not invent one. MirrorPSX copies the code into a mutex-protected
`Snapshot` and gives the pairing phase priority over video/fullscreen.

In `src/ui/screen.cpp`, pairing displays an ImGui modal centered relative to
the actual host viewport, with four large digit cells. All text and icons
continue to be drawn through the same Dear ImGui Vulkan renderer. The Stop
receiver action remains available in the pairing dialog.

## Why the previous PIN could disappear

UxPlay can open and close multiple RTSP TCP sockets while authorizing the same
device. The original callback cleared `Snapshot::pin` on **every**
`conn_destroy`, sometimes before the user had a chance to read it. The PIN
now remains during those transient disconnects. It is cleared when a client
registration succeeds, when the mirror transport starts, or when the receiver
stops. New pairing requests replace any previous PIN.

Windows also virtualized non-DPI-aware app window dimensions at 150% scaling:
the app could believe the window was much wider than the actual visible screen.
The Windows host now opts into Per-Monitor V2 DPI awareness before GLFW
initialization. The PIN dialog is anchored to the ImGui viewport rather than
the video preview panel, so it cannot vanish off its right edge.

## Validation

The `screen_render` automated test renders a sample pairing PIN with leading
zeros at 1280×720, 1920×1080, 2560×1400 and 3840×2160. It verifies popup
activation, frame geometry validity, and closure when `Snapshot::pin` clears.
The PIN shown in that test is **synthetic**, not a device's actual pairing PIN.

During Windows runtime investigation, an RTSP `/pair-pin-start` request
reached the UxPlay handler and generated a PIN. The test machine subsequently
rendered **actual decoded Apple-device video** over Vulkan. This validates
basic AirPlay reception but does not constitute a complete suite of physical
device pairing/retry tests. Runtime logs intentionally redact generated PINs.

## If pairing still fails

1. Keep the receiving PC window visible; request mirroring again from the
   Apple device and enter the four digits in the centered dialog.
2. Confirm both devices are on the same LAN with working Bonjour/mDNS.
   Screen mirroring does not work through all isolated guest Wi-Fi networks.
3. Check whether the displayed connection state shows a new incoming client
   or any transport error. If the PIN popup is absent and no request reaches
   UxPlay, inspect discovery/network connectivity before changing the UI.
4. If the service fails to listen on Windows, run
   `netsh interface ipv4 show excludedportrange protocol=tcp` to inspect
   port reservations; MirrorPSX now selects an unreserved ephemeral port.
5. Close and reopen the app to clear an abandoned pairing session. This will
   also end any active stream.

No PIN value needs to be shared publicly or attached to a bug report.
