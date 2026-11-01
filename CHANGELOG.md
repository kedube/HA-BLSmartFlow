# Changelog

## Unreleased

- Added the BLLED controller's Home Assistant control mode. **Fan 1** and **Fan 2** fan entities set the speeds directly, and **Control mode** (also **Control Mode** in the Home Assistant section of Settings) decides what they do: in **Hybrid**, the default, the fans follow their settings until Home Assistant sets a speed, then keep Home Assistant's speeds until the next print starts or you press **Resume printer control**; in **HomeAssistant**, they always run at Home Assistant's speeds, kept through a restart; in **Printer**, they always follow their settings and the fan entities are unavailable. Turning off the broker connection returns the fans to their settings. Settings shows **Home Assistant** under the fan indicators while it sets the speeds.
- Added Home Assistant entities from the BLLED controller: a **Home Assistant override** sensor, a **Printer error** sensor for filament runout, a removed front cover, nozzle or bed temperature failures, and Fatal or Serious HMS codes, with the HMS code as an attribute, a **Restart** button, and **WiFi signal**, **Free memory**, and **Largest free memory block** diagnostics, updated every minute. The Controller Log records each change in the printer's error state.
- **Runtime Status** now counts only memory that ordinary allocations can use, as the BLLED controller does. Free memory reads about 50 kB lower, and the largest free block can too: both used to include instruction RAM that the controller's buffers can't use.
- The **Firmware Update** page now asks for the `.bin.ota` file from `.firmware`, like the BLLED controller's. Its file picker used to hide `.bin.ota` files and offer the `.bin` beside them, a full flash image for USB that an update rejects. It no longer offers `.bin.gz` files either, which the controller can't unpack.
- Added **Wi-Fi & Printer Setup**, under **Device Tools**, so Wi-Fi no longer needs USB. **Find Networks** lists nearby networks with their signal, and **Find Printers** lists the Bambu Lab printers on your network; choosing one fills in its IP address and serial number. The printer settings moved here from Settings, and it replaces **Wi-Fi Setup over USB** in **Device Tools**. The USB setup still works, and still clears a forgotten login; **Web Page Login** links to it.
- Added **Controller Name**, on the **Wi-Fi & Printer Setup** page, to tell several controllers apart, as on the BLLED controller. It names the device in Home Assistant, which was **BL Smart Flow+** for every controller, and sets the `.local` address, which was always `blflow.local`: `Garage P1S` is at `http://garage-p1s.local/`, as the field shows while you type. Saving a new name restarts the controller at that address, and Home Assistant gets the name when it reconnects. Without a name, both keep their earlier defaults.
- Without Wi-Fi, the controller now opens its own setup network, **BLSmartFlow-XXXX**, which leads phones to the setup page. This happens when no network is saved, or when the saved one can't be joined at startup; before, the controller then waited, offline, until it was restarted. While no device is connected, it retries the saved network every minute and restarts normally once it joins.
- An open Wi-Fi network, with no password, can now be used.
- The **Wi-Fi & Printer Setup**, **Backup & Restore**, **Firmware Update**, and **Controller Log** pages show the fan beside their title, as the BLLED controller's pages show its logo. Settings shows the live fans there already.

## 1.5.1 — 2026-10-31

- Redesigned Settings after the BLLED controller's pages, keeping the blue background: one page with collapsible sections (Fan Control, Fan Curves, Printer, Home Assistant, Web Page Login, Debug & Diagnostics, and Device Tools), one open at a time. The Fan Graph tab is now the **Fan Curves** section, and pressing Enter in a point's fields applies the point. Switches sit on the right of their settings, and save confirmations appear in the middle of the page.
- Added a status bar showing the Wi-Fi signal, the printer connection, and the Home Assistant connection, with details on hover.
- Added **Debug & Diagnostics**, which gathers the debugging switches and adds Wi-Fi transmit power, Wi-Fi modem sleep, and switches to turn off the printer connection or SSDP discovery. These apply when saved. **Runtime Status** shows the Wi-Fi network and signal, printer reports per minute, Home Assistant, free memory, uptime, and why the controller last started, including crashes, brownouts, and watchdog resets.
- Added **Device Tools**: Firmware Update, **Backup & Restore**, a live **Controller Log** in the browser, Wi-Fi Setup over USB, and **Restart Controller**. Downloading a backup requires a Web Page Login because the file contains your passwords. A restore replaces the settings only with a complete settings file.
- Everything the controller logs now also goes to the Controller Log, which keeps the last 8 KB in memory.
- The update, backup, and log pages share the new style.

## 1.4.13 — 2026-10-31

- Added **Home Assistant** monitoring and control over a second MQTT connection, to your broker. MQTT Discovery creates sensors for nozzle temperature, both fan speeds, Post Print countdowns, and the printer connection, plus controls for synchronization and each fan's mode, static speed, printer fan source, and Post Print settings.
- Home Assistant changes are validated, written to flash, then applied, like **Save**. Fan 2's controls are unavailable while the fans are synchronized. The connection reports offline through its last will, and it republishes discovery when Home Assistant restarts.
- The broker password is never sent to the web page. It must be re-entered after you change the broker host. The connection is plain MQTT; TLS is not supported, because a second TLS session leaves the ESP32 short of memory.
- Added an optional **Web Page Login** that protects every page and request, including firmware updates. Settings warns while no login is set, and the USB Wi-Fi setup clears a forgotten login.
- Requests that change settings, curves, or firmware must now come from the controller's own pages, so another website can no longer change or re-flash the controller through your browser.
- Firmware updates now check the login before writing anything, keep the current firmware running after a failed or interrupted update, and show the result. The update page is now served at `/update`, linked from Settings.
- An open Settings page now reloads when settings change elsewhere, and it warns instead of discarding unsaved edits.
- Fixed the status LED's blink pattern pausing the controller for up to two seconds on every pass while the printer was unreachable, which delayed web requests.
- Printer reconnects no longer run on every pass. They are retried every 3 seconds at first, slowing to every 30 seconds, and each attempt stops after 3 seconds instead of 30.
- Fixed Wi-Fi setup over USB: 63-character passwords and 32-character SSIDs were truncated, overlong fields could overflow memory, and a missing field could crash the controller. The Wi-Fi password and access code are no longer echoed into the setup log.
- The controller no longer halts with its fans off if the `.local` name service fails to start.
- Pinned Chart.js to version 4, so a future major release cannot break the Fan Graph.
- Generated web page headers no longer change with the build machine's Python version.
- Pinned the build to the `espressif32@6.9.0` platform (Arduino-ESP32 2.0.17), because the ESP32SSDP library does not build on newer platforms.
- Fixed builds failing with "opening dependency file … No such file or directory" while VS Code was open. The build number is now written to `platformio.ini` after the firmware image is built instead of before compiling, which had made the PlatformIO extension delete the build directory mid-build. A failed build no longer uses up a build number.

## 1.3.12 — 2026-10-23

- Made fan settings percentage fields compact for three digits and Post Print duration fields for four digits, with room for the number controls. Changed the duration label from "s" to "seconds".

## 1.3.11 — 2026-10-23

- Widened the Printer Information text fields and matched their input font size to the fan settings fields.

## 1.3.10 — 2026-10-23

- Widened Settings to match the Fan Graph panel on desktop, kept the mobile layout, and aligned the firmware footer with both panels.

## 1.3.9 — 2026-10-23

- Moved save notifications to the top right of the page.
- Updated Printer Information with matching label typography, right-aligned input fields, no trailing colons, and more space beneath the heading. Fields stack on narrow screens.

## 1.3.8 — 2026-10-23

- Matched the debugging switches' label font and row spacing to the fan controls.
- Added versioned OTA filenames such as `build/esp32dev/firmware-1.3.8.bin` to the build output.

## 1.3.7 — 2026-10-23

- Aligned settings switches on the left with their descriptions beside them and parameter controls on the right. Removed the Synchronize Fans On/Off labels.
- Simplified independent fan headings to **Fan 1** and **Fan 2**, and hid the fan heading in synchronized mode.

## 1.3.6 — 2026-10-23

- Added independent **Post Print** switches, durations in seconds, and speed percentages. Timers temporarily override static speed, printer fan following, or the temperature curve after a running print finishes, fails, or becomes idle.
- Added live Post Print countdowns below the fan percentages. Timers clear on reset or Wi-Fi/MQTT loss and do not restart from repeated end reports or an end state received after reconnecting.
- Preserve both fans' Post Print settings across synchronization changes and restarts; existing configurations default the feature to Off.
- Made the firmware version footer half its previous text size and left-aligned.
- Added timer, connection-loss, persistence, input-validation, and UI regression coverage.

## 1.2.5 — 2026-10-23

- Reserved an empty source-label line beneath fans that are not following, keeping the two fan images aligned.
- Matched Bambu Studio's 10% speed steps for part, auxiliary, and chamber fan reports, fixing cases where a printer setting of 70% appeared as 71%. Static speeds and temperature curves retain their existing precision.

## 1.2.4 — 2026-10-23

- Added **Follow Printer Fan** for each output, with Part Cooling, Auxiliary Cooling, Chamber/Exhaust, and Hotend/Heatbreak sources. Enabling it turns off Static Fan Speed, and enabling Static Fan Speed turns following off. Both can be off to use the temperature curve.
- Added a white source label below each following fan's speed indicator, and enlarged both override labels.
- Decode packed and separate MQTT fan-speed fields, retain speeds across partial updates and disconnects, and use 0% until the first valid report after boot.
- Persist follow settings independently for both outputs, preserve dormant Fan 2 settings during synchronization, and default following to Off for existing configurations.
- Added MQTT parsing, output control, persistence, invalid-input, and UI regression tests.

## 1.1.3 — 2026-10-23

- Removed the separate Fan control heading and renamed the switch label to **Synchronize Fans**, using heading-size text beside the switch.

## 1.1.1 — 2026-10-23

- Replaced the synchronization and curve selectors with graphical switches. Fan labels consistently use **Fan 1** and **Fan 2**, with white speed percentages for both indicators.
- Clicking a curve point now selects its fan and opens the point editor. Overlapping points prefer the currently selected fan.
- Replaced the save dialog with a notification that disappears after four seconds. Failed saves continue to show an error.
- Changed the displayed name on the web pages to **BL Smart Flow+**.
- Adopted major.feature.build versioning, with an automatic build-number increment and build date for each firmware build.

## 1.1.0 — 2026-10-23

- Added **Both fans synchronized**, defaulting to On for new and existing configurations.
- Added independent fan curves and static-speed overrides. Turning synchronization On uses Fan 1's settings for both outputs while preserving Fan 2's settings for later use.
- Added two labeled curves on one chart, a fan editing selector, separate feature sections with inline static-speed controls, and two independently animated fan indicators.
- Persisted both fans' static settings and curves, validated curve updates, and applied web changes only after a successful flash write.
- Added migration, GPIO output, save-failure, and UI regression tests. Invalid saved curves now fall back to defaults without deleting the configuration file.

## 1.0.3 — 2026-10-23

- Added a transparent favicon by overlaying `smartflow_blades.png` on `smartflow_shell.png`, with 16, 32, 48, and 64 pixel sizes.
- Embedded the favicon in the firmware, served it at `/favicon.ico`, and linked it from the HTML pages.
- Updated the build script to regenerate the favicon header alongside the compressed HTML headers.

## 1.0.2 — 2026-10-23

- Fixed blank settings fields by loading configuration immediately instead of waiting for external images to finish loading.
- Added a visible error when settings cannot be loaded and disabled Save until loading succeeds, preventing accidental overwrites with empty values.
- Stopped logging submitted credentials in the browser console.
- Added regression tests for settings loading, failed requests, and saving before settings arrive.

## 1.0.1 (retracted) — 2026-10-23

Retracted because the settings page could remain blank while waiting for external images to load. The saved configuration was still present; the loading problem was fixed in 1.0.2.

- Fixed web settings persistence so changes to the printer access code are written to flash and survive a restart.
- Confirmed a successful save only after writing the configuration, and returned an error when saving fails.
- Added temporary-file writes to protect the previous configuration against incomplete writes, and bounded JSON parsing to the configuration file's size.
- Updated the displayed firmware version and date, regenerated the HTML headers, and made future builds regenerate the page headers used by the web server.
