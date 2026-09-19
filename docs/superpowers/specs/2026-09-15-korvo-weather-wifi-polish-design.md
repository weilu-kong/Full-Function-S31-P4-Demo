# Korvo Weather and Wi-Fi Polish Design

## Goal

Make Wi-Fi persistent across scene changes and finish the weather and saved-network UI so it is usable and unambiguous on the Korvo-1 display.

## Confirmed behavior

- A connected station remains connected when the user returns home or reopens Weather.
- Every scene shows the same Wi-Fi enabled state. A newly loaded scene must not interpret its default-off toggle as a user disconnect request.
- A saved secure network opens an action sheet that identifies the SSID and saved state, then offers `接続`, `このネットワーク設定を削除`, and `閉じる`.
- Scan rows use one lock glyph for secure networks, not an asterisk or `[LOCK]` text.
- The Weather status label does not collide with its separator. Its top time is Japan local time in `HH:MM` and updates once a minute after SNTP is valid.
- Wi-Fi and volume icons in the Weather status bar are derived from shared runtime state; four Wi-Fi states are off, connecting, connected, and failed.

## Design

### Wi-Fi state ownership

`s_wifi_enabled` remains the single source of truth. On every scene change, the newly instantiated `wifi_enabled` control is set from it before the 50 ms poll can treat its default value as input. A short synchronization guard makes this programmatic update invisible to the poll and callback path. Only an explicit user toggle changes `s_wifi_enabled` and calls `esp_wifi_disconnect()`.

This preserves the existing quick-settings UX and avoids relying on the unreliable GSP toggle callback.

### Saved secure network sheet

The selected saved SSID is bound into the sheet title. The sheet labels it as a saved network and uses the same lock glyph as scan rows. `接続` invokes the stored Wi-Fi configuration without asking for a password; deleting the network clears the stored station configuration and closes the sheet.

### Weather time and status

The common top-clock binding is refreshed by the UI timer while the Weather scene is active. It formats local time after SNTP synchronization; before then it remains clearly in DEMO/offline state. Weather condition and update text are moved so their bounding boxes do not overlap the existing separator.

### Shared status icons

The Weather scene receives small bound status labels/icons rather than relying on art baked into the background. Wi-Fi maps to off, connecting, connected, or failed. Volume maps to muted or audible based on the current output setting. These values are updated by the existing UI timer and on state transitions.

## Verification

1. Automated scene checks verify the saved-network sheet labels, lock representation, weather layout bounds, clock binding, and shared-state code paths.
2. Build a clean ESP32-S31 / 16 MB firmware image.
3. Flash it and read back an application segment to verify the expected worktree image is resident.
4. On hardware: connect Wi-Fi, return home, reopen Weather, and verify the connection and weather refresh persist.
5. On hardware: verify the saved-network sheet, delete path, secure lock glyph, clock, non-overlapping weather text, and all four Wi-Fi status states.

## Scope

No changes to Wi-Fi credentials, weather API provider, partition layout, or keyboard geometry are included.
