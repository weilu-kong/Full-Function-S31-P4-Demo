# Weather Native Status Design

## Goal

Replace the weather screen's temporary top-right patch with the approved full
day/night weather artwork and a native dynamic header. Restore the top-edge
Quick Settings drawer gesture.

## Rendering

- Use `yokai_weather_day_800.png` as the daytime base artwork and
  `yokai_weather_night_800.png` as the nighttime base artwork.
- Select the artwork with Open-Meteo `is_day`; when live weather is unavailable,
  use Japan local time (06:00–17:59 day, otherwise night).
- Keep the two embedded PNG payloads immutable and submit them via the GSP
  borrowed-image API, rather than copying a ~700 KB payload into heap memory.
- The scene's authored initial image is the daytime artwork, so the first frame
  is never the obsolete `yokai_weather_sunny_800.png`.

## Header and status

- Delete the opaque `weather_header` patch rectangle and obsolete duplicate
  Wi-Fi label.
- Draw back affordance, `天気`, `LIVE`, time, Wi-Fi glyph, and volume glyph/value
  as native scene components over the artwork's intentionally blank header.
- Refresh the header on scene entry, Wi-Fi state changes, and volume changes;
  the minute timer remains only for clock progression.
- Wi-Fi states are disconnected, connecting, connected, and failed, sourced
  from the shared Wi-Fi connection state. Volume is sourced from the shared
  Quick Settings slider value.

## Gestures

- The top-edge drawer remains the sole owner of the top pull gesture.
- Removing the full-width header rectangle removes the hit region that overlaps
  the Drawer acquisition zone. Scene horizontal swipe remains disabled.

## Verification

- Source regression checks assert the day artwork is authored as initial image,
  no `weather_header` rectangle exists, and the borrowed-image path is used.
- Build the ESP32-S31 firmware and confirm partition size.
- On hardware: pull down from the weather screen top edge, change volume and
  Wi-Fi state, return to weather, and verify the native header and day/night
  selection.
