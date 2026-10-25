# RE6 Save Editor

A save editor for **Resident Evil 6 (PC/Steam)** `savedata.bin`, written in C++20 with a native Qt 6 Linux interface and the original Windows version preserved.

- Skill points, owned skills, and equipped skill loadouts (Campaign, Mercenaries, Siege, Onslaught, Survivors, Predator)
- Character and costume unlocks, Mercenaries stage unlocks (including No Mercy), Serpent Emblems
- Mercenaries records (solo and duo) and per character inventory
- One click **Unlock Everything**
- Checksum is recalculated automatically, and a `.bak` backup is written before the first save

## Download

For Windows, download `RE6SaveEditor-<version>-win64.zip` from the [Releases](../../releases) page, unzip it, and run `RE6SaveEditor.exe`.
It's a portable executable with no installer and no runtime dependencies (Windows 10 or later).

## Linux (AppImage)

A native Qt-based Linux build is provided alongside the Windows version. The AppImage is produced by GitHub Actions and does not require Wine.

- Download the AppImage from the [Releases](../../releases) page.
- The latest CI build is also available from [GitHub Actions](../../actions/workflows/linux-appimage.yml).
- Make the downloaded file executable and run it:

```sh
chmod +x RE6SaveEditor-*-x86_64.AppImage
./RE6SaveEditor-*-x86_64.AppImage
```

The Open dialog looks for `savedata.bin` in common Steam userdata locations for Resident Evil 6 (app ID `221040`). If a candidate is found, its full path is preselected in the dialog; verify that it is the right save before clicking Open. Detection supports standard Steam paths, Flatpak Steam, `XDG_DATA_HOME`, and a custom Steam root supplied via `STEAM_DIR`. If no save is found, choose it manually.

For a custom Steam installation, run the AppImage with the Steam root set explicitly, for example:

```sh
STEAM_DIR="/path/to/Steam" ./RE6SaveEditor-*-x86_64.AppImage
```

The Linux version edits unencrypted Resident Evil 6 PC/Steam saves. It recalculates the checksum when saving and creates a `.bak` backup beside the original save on the first save to that path in the session. Console saves and encrypted saves are not supported.

## Tests

The default CTest suite uses synthetic data and does not require a personal save file. To additionally run the real-save regression locally, configure with a path to your own save:

```sh
cmake -S . -B build -DRE6_SAVE_FIXTURE=/absolute/path/to/savedata.bin
cmake --build build
ctest --test-dir build --output-on-failure
```

The real-save regression only reads the supplied fixture and performs all writes on a temporary copy. **Do not commit personal `savedata.bin` files**; save files are user data and are intentionally excluded from this public repository.
