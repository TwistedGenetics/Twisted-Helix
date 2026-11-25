# Downloads

| File | What |
|---|---|
| [HELIX-Manual.pdf](HELIX-Manual.pdf) | The user manual (version 0.5.1): installing, every page and control, presets, the MPC and its new Concrete Rack screen, CPU, troubleshooting, parameter reference. |
| `helix-0.5.1-mpc.zip` on the repository's **Releases** page | HELIX 0.5.1 for Akai standalone MPCs (MPC OS 3.10.1+), with the Massive-style touch screen. One folder for Gen1 32-bit (One / One+ / One Retro / Live II / X / Key / Force) and Gen2 64-bit units. Built by the release workflow. |
| [helix-0.1.0-mpc.zip](helix-0.1.0-mpc.zip) | **Old, do not use.** Its screen file (`Plugin Skins/TUI.json`) is faulty: the MPC can reject it and show no HELIX pages at all. Use 0.5.1. |

**Untested on hardware.** It passes the MPC Plugin SDK's validator and host simulator. Save your projects often:
third-party plugins run inside MPC with no protection.

Install:
1. Unzip.
2. Copy the folder `Twisted Genetics - VST - HELIX` into a folder named exactly `VST2 Plugins` at the root of a USB
   stick, SD card or the MPC's internal drive.
3. On the MPC: Preferences → Enhancements (Beta) → turn on **Enable Enhancements** and **External Plugin Support**.
4. HELIX appears under **External Plugins** in the plugin browser.

Full instructions are in `INSTALL.txt` inside the zip. Releases (with a macOS desktop build) are on the
repository's Releases page once the release workflow has run.
