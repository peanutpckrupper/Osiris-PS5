# Osiris

**Play the Halo 5 campaign offline — on PC, and on a jailbroken PS5 — built from your own game files.**

Osiris is a compatibility layer that turns a copy of the free **Halo 5: Forge** (PC) and a dump of **Halo 5: Guardians** (Xbox One) into a standalone, offline campaign you can run on Windows or on a jailbroken PlayStation 5. The PS5 is the first time Halo 5's campaign has been made playable on the console.

It ships **no game files**. You bring your own copies; a setup tool combines them on your PC into a game folder you can play from or copy to the console.

> **Built on [halo5onpc](https://github.com/danielmccluskey/halo5onpc) by Daniel McCluskey.** The work of getting the Halo 5 campaign running on PC — the Forge-unpackaged approach, the bake/worker, the UI-residency handling — is his. Osiris's own contribution is the **PS5 port** (prospero-win integration, the Wine/vkd3d fixes) and the offline packaging. You'll need halo5onpc too; see [Credits](#credits--third-party).

---

## ⚠️ Read first

- **This is an unofficial fan project.** It is not made, endorsed, or supported by Microsoft or 343 Industries / Halo Studios.
- **It contains no game content.** You must own and provide your own copies of **both** Halo 5: Forge and Halo 5: Guardians. Osiris only patches *your* files; it never distributes theirs.
- **Offline only.** There is no online play, matchmaking, Forge sharing, or file share. You play locally as "Spartan."
- **Halo is a trademark of Microsoft.** "Osiris" is just the project name.

If you don't own both games and can't produce your own dumps, this project is not for you.

---

## What you need

### To build the game folder (on a Windows PC)
- Windows 10/11, x64
- Your **Halo 5: Forge** installation (Forge is a free download; this is the PC runtime Osiris runs on)
- Your own dump of **Halo 5: Guardians** (Xbox One) — the folder containing `game_Release.exe`, `deploy\`, and `sound\Durango\`
- ~100 GB free (the finished game folder is ~95 GB)

### To play on PC
- Just run the game folder you built. A controller is recommended.

### To play on a PS5
- A **jailbroken PS5** with [prospero-win](https://github.com/mpereiraesaa/prospero-win) and ShadowMountPlus (to install homebrew from `/data/homebrew`)
- An FTP client on your PC and ~100 GB free on the console

---

## Installation

### 1. Build your game folder

Download **`Osiris-Setup-1.0.zip`**, extract it, and run `OsirisSetup.exe`. Point it at your two dumps. It produces a `Halo5Campaign` folder (~95 GB) with everything needed to play.

### 2a. Play on PC

Run `Osiris.exe` inside that folder. It starts fullscreen (Alt+Enter toggles windowed). Main menu → **Campaign → Solo**. Missions unlock as you play, just like on Xbox.

### 2b. Play on a PS5

1. Download **`Osiris-PS5-1.0.zip`** and upload the `PPSA99905` folder to `/data/homebrew` on the console. ShadowMountPlus adds **Osiris** to the home screen.
   - *If it won't start after FTP:* set `eboot.bin` and `sce_module/libc.prx` to `755` (your FTP client's permissions/chmod option).
2. Open it once. The first launch copies the app's Windows runtime into `/data/halo5campaign` (black screen up to a minute), then closes — expected, there's no game folder yet.
3. Upload the contents of your `Halo5Campaign` folder to `/data/halo5campaign/Halo5Campaign` (~95 GB; FileZilla can resume).
4. Open **Osiris** and play.

Full step-by-step is in the PS5 release's `README.txt`.

---

## Controls & settings

- **Quit:** hold **View + LB + RB** (Back + both bumpers — **Share + L1 + R1** on a PlayStation controller) for 3 seconds, or **Alt+F4** on PC.
- **Frame rate & FOV:** Settings → Audio & Video adds **144 and 180 FPS** options and a **campaign field-of-view slider (60–120)** on top of the stock 30/60.
- **Saves & options** live in the `h5campaign` folder inside your game folder, so they travel with it. Back it up to keep your progress.

---

## How it works

Halo 5: Forge is a UWP (Microsoft Store) app built against Xbox services. Osiris makes it run as an ordinary offline game:

- **`ForgeDesktopShim.dll`** stands in for the Windows Runtime, Xbox Live, and Store APIs the game expects — faking an offline "Spartan" sign-in, a full offline licence, local save storage, and the windowing/input/display plumbing — so the game never needs an account, the network, or the Store.
- **`memory_release_uwp.dll`** is an in-process loader that bootstraps the shim inside the game before its entry point runs.
- The **campaign content** is reconstructed from the Guardians dump by hash-verified byte-patch recipes applied at setup time (no game files are shipped).
- Extras like the **144/180 FPS options** and the **FOV slider** are added by patching the engine's frame-pacing and menu tables.

On the **PS5**, the same shim runs under [prospero-win](https://github.com/mpereiraesaa/prospero-win) (Wine for the PS5) with Direct3D 12 translated to Vulkan via vkd3d-proton on Mesa/RADV. Getting there required filling gaps the console's Wine doesn't provide — a Text Services stand-in, Windows Runtime collection types the offline sign-in needs, a `PropertySet` replacement — and a workaround for a driver limit where the game's large copy-only GPU command lists exceed what one submission can hold (an empty dispatch is injected to give the driver a split point). A fix for the mission-exit path keeps the menu stable when you quit a mission.

---

## Known limitations

- **Offline only** — no multiplayer, matchmaking, Forge browser, or file share.
- Some HUD/UI elements log missing resources (Guardians vs. Forge tag differences); they fall back to defaults and don't affect gameplay.
- Performance on the PS5 varies by scene; the campaign is playable end-to-end, but this is community compatibility work, not an optimized native port.

---

## Building from source

The compatibility layer (`forge-shim/`) builds with MSVC + the Windows SDK; the setup and bake tools (`tools/`) build with the .NET SDK. See `NOTES.md` and `tools/h5-runner.ps1` for the build/stage/package steps. The public release zips are produced by the `package` job.

---

## Credits & third-party

Osiris builds on a lot of other people's work:

- **[halo5onpc](https://github.com/danielmccluskey/halo5onpc)** by Daniel McCluskey — the upstream project that made the Halo 5 campaign playable on PC. Osiris is an extension of it for the PS5; you need halo5onpc to build a game folder. Please support his work and respect its terms. **Get it from his repo, not from here.**
- **[prospero-win](https://github.com/mpereiraesaa/prospero-win)** by mpereiraesaa and contributors — Wine for the PS5
- **Wine**, **DXVK**, **[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton)**, and **Mesa / RADV** — the Windows and Direct3D 12 → Vulkan runtime on the console
- **WinDurango** — UWP/Xbox API groundwork the shim draws on
- **[MinHook](https://github.com/TsudaKageyu/minhook)** — function hooking

Each is used under its own licence; see the `LICENSES` folder in the releases and the licence files in this repo.

---

## Legal

This project interoperates with software you already own and does not include, host, or link to any copyrighted game content. It is provided for people who own the games to run their own copies. Halo, Halo 5, and related marks are trademarks of Microsoft. If you are a rights holder with a concern, open an issue.
