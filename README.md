# ClaudeFor3DS

Claude and a Claude Code-style coding agent on the Nintendo 3DS. It uses the official Anthropic API with your own API key.

The newest build is always at the root of this repository: [`ClaudeFor3DS.cia`](ClaudeFor3DS.cia). Every push builds it with GitHub Actions.

## Install

1. Download `ClaudeFor3DS.cia` from this repository, or download the `.cia`/`.3dsx` from the latest Actions run.
2. Install the `.cia` with FBI. It shows up on the HOME Menu with a red Claude icon.
   Or copy `ClaudeFor3DS.3dsx` to `sdmc:/3ds/` and start it from the Homebrew Launcher.
3. You need Wi-Fi set up in System Settings.

## Sign in

ClaudeFor3DS signs in with an **Anthropic API key**. API usage is billed per message and is separate from a Claude Pro/Max subscription.

1. Create a key at [console.anthropic.com](https://console.anthropic.com) under **API Keys**. This works from a phone browser.
2. Either:
   - choose **Type API key** and enter it on the keyboard, or
   - save the key in a text file at `sdmc:/3ds/ClaudeFor3DS/api_key.txt`, then choose **Load api_key.txt from SD card** (or press Y).
3. The app checks the key, encrypts it with a key unique to your console (AES-256-GCM, stored in `session.bin`) and deletes `api_key.txt`. A copy of `session.bin` won't work on another 3DS.

You stay signed in across launches. **Menu → Sign out** removes the key from the console.

There is no claude.ai email or subscription sign-in. Anthropic doesn't allow third-party apps to sign in to Claude accounts or to use subscription credentials ([Claude Code legal and compliance](https://code.claude.com/docs/en/legal-and-compliance)), so this app uses the API instead.

## Screens

- **Top screen:** the conversation, under a status bar that is always visible. The status bar shows the clock, Chat/Code mode, the current model and effort, a usage meter with the percentage left and its reset time, Wi-Fi strength and the battery level (with a charging indicator).
- **Bottom screen:** the controls.
  - Chat/Code toggle and the model and effort pickers.
  - Three panels:
    - **Activity:** thinking, web searches, commands, file edits, connector calls, subagents and created files, live.
    - **Queue:** messages waiting to be sent, each with a remove button.
    - **Tasks:** Claude's task list with progress.
  - The message box, with **Send** and **Stop** buttons.

## Controls

| Button | Action |
| --- | --- |
| **Y** / tap the message box | Type a message. While Claude is responding, the message is queued. |
| **B** | Stop Claude. Queued messages pause until you resume them. With nothing running: deselect. |
| **A** | Actions for the selected message: read in full, view images, save files, open artifacts, tool details, links as QR codes, retry |
| **X** / ☰ button | Menu: new chat, chats, mode, model, effort, queue, usage, settings, sign out |
| **SELECT** | Switch between Chat and Claude Code mode |
| **D-pad Up/Down** | Select messages |
| **D-pad Left/Right** | Page up / down |
| **Circle Pad / C-Stick** | Scroll smoothly |
| **L / R** | Switch the bottom panel (Activity, Queue, Tasks) |
| **START** | Quit (asks first if Claude is still responding) |

Everything also works by touch: tap chips, tabs, list rows and buttons, and drag lists to scroll.

When Claude asks a multiple-choice question, a sheet opens with the options. Choose with the D-pad and A, by touch, or pick **Type your own answer**. For multi-select questions, A toggles an option and X submits.

## Modes

- **Chat:** Claude with web search and web fetch. Effort and adaptive thinking follow your settings.
- **Claude Code:** Claude works in Anthropic's code execution sandbox, running bash and editing files. Files and installed packages persist across the conversation. Claude keeps a task list you can follow in **Tasks**. Files it creates can be previewed (images) and saved to `sdmc:/3ds/ClaudeFor3DS/files/`.

Conversations are saved per mode on the SD card in `sdmc:/3ds/ClaudeFor3DS/conversations/`.

Content the 3DS can't render shows as a clearly labelled placeholder, which you can open to see the raw data. This covers emoji, interactive artifacts and unknown block types.

## Settings and config

In-app, under **Menu → Settings**:
- text size
- thinking previews
- following new messages
- staying awake while Claude works
- web search
- time zone
- the log

`sdmc:/3ds/ClaudeFor3DS/config.json` also accepts:

```json
{
  "custom_instructions": "Answer in French.",
  "connectors": [
    { "name": "github", "url": "https://example.com/mcp", "authorization_token": "..." }
  ]
}
```

`connectors` are remote MCP servers passed through Anthropic's MCP connector. Their calls appear in Activity under the connector's name.

## Building

Requirements:
- devkitPro with devkitARM, libctru, citro2d/citro3d and the 3DS portlibs (curl, mbedtls, zlib)
- `makerom` and `bannertool` for the `.cia` (`scripts/install_cia_tools.sh` installs both)

```bash
make            # ClaudeFor3DS.3dsx and ClaudeFor3DS.cia
make 3dsx       # just the .3dsx
```

`python3 tools/make_assets.py` regenerates the icon and banner art.

The desktop simulator renders every screen to PNG files for checking layout without a console:

```bash
make -f host/Makefile && host/build/claude3ds-sim shots
```

The GitHub Actions workflow builds in the `devkitpro/devkitarm` container on every push and uploads both files. On `main`, it also commits `ClaudeFor3DS.cia` to the repository root with `[skip ci]`, so that commit doesn't trigger another build.

## Credits

[TriCord](https://github.com/2b-zipper/TriCord) inspired the look and the top/bottom screen split. ClaudeFor3DS shares no code with it.

Bundled third-party libraries:
- rapidjson (MIT)
- stb_image (public domain/MIT)
- qrcodegen (MIT)

Linked from devkitPro:
- libctru, citro2d, citro3d (zlib)
- libcurl (curl license)
- mbedTLS (Apache-2.0)

CA certificates come from Mozilla's bundle (MPL-2.0).

This is an unofficial project, not affiliated with or endorsed by Anthropic or Nintendo.
