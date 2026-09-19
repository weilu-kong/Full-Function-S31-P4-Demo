# Korvo-1 Weather, Wi-Fi, Status, and Day/Night Theme Design

## Scope

Finish the observed Weather and Wi-Fi defects, replace static status icons with shared live state, and add a paired day/night Weather theme. The work is limited to the Korvo-1 firmware and its assets.

## Shared status model

One UI-side status snapshot is the source of truth for every scene:

- Wi-Fi has four render states: disconnected, weak, medium, strong. A connected station maps its live RSSI to the three signal levels; disconnected renders the offline icon.
- Volume has four render states: muted, low, medium, high. It reads the actual speaker output volume and treats zero as muted.
- The UI refreshes the visible scene whenever Wi-Fi connection/RSSI or speaker volume changes. Every existing scene uses the same icon state, so scene transitions cannot leave stale symbols behind.
- The two generated Weather backgrounds contain only decorative, non-live UI space. Clock, Wi-Fi, and volume remain runtime elements.

## Weather screen

- Remove the additional `weather_clock` object. The existing top-bar system clock is the sole displayed time.
- Set system time from SNTP after Wi-Fi obtains an IP. Render local JST from the system clock. Before valid synchronization, retain the existing placeholder rather than presenting a second incorrect time.
- Make the existing left-card `更新 HH:MM` row the manual refresh action. No refresh control is placed in the right status area.
- Weather theme selection uses Open-Meteo's `is_day`. If unavailable, use JST `06:00 <= time < 18:00` as the fallback.
- Add two runtime-selectable, separately illustrated 800x480 assets: a day scene and a night scene. The approved artwork retains the detailed pixel-art / ukiyo-e Yokai, Mt. Fuji, village, left information panel, top status bar, and bottom navigation visual language. Night has moonlight, lanterns, and water reflections; it is not an overlay or filter.

## Wi-Fi flow

- ESP-IDF's persistent STA configuration stores the most recently connected encrypted SSID and its credentials. This is the explicit one-network policy.
- In a scan result, an SSID equal to that stored configuration opens a saved-network operation drawer: `接続`, `このネットワーク設定を削除`, and close.
- `接続` reconnects with the existing STA config without clearing or replacing its password. It must not call the new-connection helper with a null password.
- `このネットワーク設定を削除` disconnects, clears the persisted STA config, and returns to the scan list. A later connection to that SSID opens the password keyboard.
- Unsaved encrypted SSIDs open the existing password drawer and keyboard. Open SSIDs keep their direct connection flow.
- Replace the scan-list ASCII `*` suffix with a small in-scene lock glyph/shape for secured rows. It occupies a fixed icon column and does not alter SSID text.
- Keep keyboard key geometry unchanged; change only the keyboard glyph font size from 16 to 32.

## Error handling

- If a saved-network reconnect fails, preserve the saved configuration and show the normal connection failure status; do not silently erase credentials.
- If forgetting the network fails, retain the visible saved-network drawer and show the failure status.
- If weather refresh has no Wi-Fi connection, retain last valid weather data and render a disconnected state rather than replacing data with an error image.

## Acceptance checks

1. Startup reaches the home UI and Weather screen without a reset loop.
2. Weather has one correct JST top-bar time; its left-card refresh control does not overlap status icons and triggers a refresh.
3. Weather switches between the two approved full scenes at day/night boundaries; the fallback works without fresh weather data.
4. Wi-Fi and volume symbols update consistently on every scene and represent the four approved levels/states.
5. Secured SSIDs show a lock, not `*`.
6. Saved SSIDs show the operation drawer; direct reconnect succeeds without password input; forget clears the behavior; an unsaved reconnect requests a password.
7. Keyboard glyphs are 32 px while key rectangles retain their present dimensions.

