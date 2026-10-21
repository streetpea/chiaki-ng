# AI Control (MCP Server)

`chiaki-ng` has a built-in [Model Context Protocol](https://modelcontextprotocol.io){target="_blank" rel="noopener"} (MCP) server. Any MCP capable AI tool (Claude Code, Claude Desktop, Cursor, VS Code, ...) can use it to see exactly what `chiaki-ng` shows and to drive both the app and the streamed PlayStation.

!!! Info "What the AI gets"

    - **Pixel perfect screenshots**: the complete window at native resolution, including the stream video, the UI overlay, letterboxing and every other visible `chiaki-ng` window (stream menu, dialogs, stats overlay). Nothing is cropped; windows that stick out of the main window are included too. HDR streams are tone mapped to normal (SDR) colors.
    - **The UI as structured data**: every button, text, field and list with its state and position.
    - **Real input**: mouse clicks, drags, scrolling and keyboard input that go through the same path as your own input.
    - **A virtual controller**: DualSense buttons, sticks, triggers and touchpad for the console while streaming.
    - **App actions**: list, wake and connect to consoles, read stream statistics, disconnect.

## Enabling the Server

Use any one of these:

=== "Settings"

    Open **Settings > Video**, check **AI Control (MCP Server)** and optionally change the **MCP Server Port** (default `9339`). The server starts and stops right away.

=== "Command Line"

    ``` bash
    chiaki --mcp                  # http://127.0.0.1:9339/mcp
    chiaki --mcp-port 9400        # different port (implies --mcp)
    chiaki --mcp-token SECRET     # require "Authorization: Bearer SECRET"
    ```

    The options work together with the `stream` command, e.g. `chiaki --mcp stream ...`.

=== "Environment"

    | Variable | Meaning |
    | --- | --- |
    | `CHIAKI_MCP=1` | Enable the server |
    | `CHIAKI_MCP_PORT=9400` | Port |
    | `CHIAKI_MCP_TOKEN=SECRET` | Require a bearer token |
    | `CHIAKI_MCP_BIND=0.0.0.0` | Listen on another address (requires `CHIAKI_MCP_TOKEN`) |

The endpoint is `http://127.0.0.1:9339/mcp` (MCP "Streamable HTTP" transport).

!!! Warning "Security"

    Whoever can reach the server can control `chiaki-ng` and your console. By default it only listens on `127.0.0.1` (this computer) and rejects requests coming from web pages. Only expose it on your network together with a token.

    Text the AI types (`type_text`) is never kept in the action history, password fields are reported as `<masked>`, and `save_path` only ever writes `.png` files.

## Checking That It Works

With `chiaki-ng` running with the MCP server enabled, run the smoke test (Python 3, no extra packages):

``` bash
python3 scripts/mcp/chiaki-mcp-smoke-test.py            # read-only checks
python3 scripts/mcp/chiaki-mcp-smoke-test.py --interact # also sends harmless input
```

It checks the connection, takes full, annotated and zoomed screenshots (saved to `./chiaki-mcp-smoke`), reads the UI, runs `mcp_self_check` and prints a pass/fail line per tool.

## Connecting an AI Tool

=== "Claude Code"

    ``` bash
    claude mcp add --transport http chiaki-ng http://127.0.0.1:9339/mcp
    ```

=== "Cursor / VS Code / HTTP clients"

    Add an HTTP MCP server pointing at `http://127.0.0.1:9339/mcp`, e.g. for Cursor (`~/.cursor/mcp.json`):

    ``` json
    {
      "mcpServers": {
        "chiaki-ng": { "url": "http://127.0.0.1:9339/mcp" }
      }
    }
    ```

    VS Code (`.vscode/mcp.json`):

    ``` json
    {
      "servers": {
        "chiaki-ng": { "type": "http", "url": "http://127.0.0.1:9339/mcp" }
      }
    }
    ```

=== "Claude Desktop / stdio-only clients"

    Use the bridge script [`scripts/mcp/chiaki-mcp-stdio.py`](https://github.com/streetpea/chiaki-ng/blob/main/scripts/mcp/chiaki-mcp-stdio.py){target="_blank" rel="noopener"} (Python 3, no extra packages). With `--launch` it also starts `chiaki-ng` when it is not running:

    ``` json
    {
      "mcpServers": {
        "chiaki-ng": {
          "command": "python3",
          "args": ["/path/to/chiaki-mcp-stdio.py", "--launch", "/path/to/chiaki"]
        }
      }
    }
    ```

    Add `"--token", "SECRET"` if you configured a token.

## Tools

| Tool | What it does |
| --- | --- |
| `take_screenshot` | Full, uncropped capture of everything `chiaki-ng` shows. Returns the image plus a description of each window's position. `save_path` additionally writes a lossless full resolution PNG. `region` zooms into part of the last screenshot at native resolution to read small text. If an image would exceed `max_bytes` (default ~3.5 MB, the limit most AI clients accept) it is sent as JPEG and, if still needed, scaled down as a whole; it is never cropped. |
| `get_window_info` | Visible windows, their positions in screenshot coordinates, window state, DPI scale, the focused control and the current `location` (open view, dialog, tab) |
| `get_ui_tree` | Outline of all visible controls with ids, text, state (`FOCUSED`, `checked`, values) and bounding boxes. `interactive_only` and `root` (one dialog or page) keep it small. Password and PIN fields are masked |
| `read_screen_text` | All visible app text in reading order with controls, check states and focus marked: a cheap way to read menus, dialogs and errors |
| `find_ui_elements` | Search controls by text, type or `objectName` |
| `click_element` / `focus_element` | Click or focus a control found by id / text / type |
| `click`, `mouse_move`, `drag`, `scroll` | Mouse input at screenshot coordinates (double click, long press, right click, modifiers) |
| `press_key`, `type_text` | Keyboard input such as `Return`, `Escape`, `Up`, `ctrl+o`, `F11` or plain text |
| `window_control` | Activate, restore, minimize, maximize, fullscreen, resize or move the window |
| `wait_for` | Wait until an element appears or disappears, the screen changes, or the screen settles, instead of guessing sleep times |
| `wait_for_stream` | Wait until the stream is connected, the console picture is visible, or the session ended |
| `wait` | Pause (optionally followed by a screenshot) while the console or an animation reacts |
| `mcp_self_check` | Tests window visibility, frame capture, UI access, keyboard focus, consoles and the stream; every check reports pass/warn/fail with a fix |
| `get_usage_guide` | Playbook for AIs: look-act-verify loop, PlayStation controller cheat sheet, timings, shortcuts, troubleshooting |
| `get_action_history` | The session's tool calls with arguments, outcome, errors and duration, so an AI can retrace what it already tried |
| `get_app_state` | Stream status (host, bitrate, packet loss, frames lost), consoles, PSN state, controllers |
| `list_hosts`, `connect_host`, `wake_host`, `disconnect_stream` | Console management, same as using the console list |
| `controller_input` | Virtual DualSense: single presses (`buttons`), sequences (`sequence: ["dpad_down", "cross"]`) or full `steps` with sticks, triggers and touchpad |
| `release_controller` | Release all virtual inputs immediately |

## Prompts

Clients that support MCP prompts (e.g. as slash commands) get ready-made workflows:

| Prompt | Arguments | Purpose |
| --- | --- | --- |
| `play_on_console` | `goal`, optional `console` | Connect if needed and do something on the PlayStation |
| `operate_chiaki` | `goal` | Do something in the chiaki-ng interface (settings, consoles, registration) |
| `diagnose_stream` | | Analyze bitrate, packet loss and picture quality and suggest fixes |

!!! Tip "Fewer round trips"

    - `take_screenshot` with `annotate: true` draws numbered boxes on every control and lists each mark's element id, so the AI can go straight to `click_element`.
    - Every action tool (`click`, `click_element`, `press_key`, `type_text`, `controller_input`, `connect_host`, ...) accepts `screenshot_after: true` (plus `screenshot_delay_ms`, `annotate_screenshot`) and returns the resulting screen in the same response.
    - The same tools accept `expect` to verify the action in one call, e.g. `click_element {text: "Open dialog", expect: {until: "element", text: "Close dialog"}}` or `controller_input {buttons: ["cross"], expect: {until: "screen_changed"}}`. If nothing happens, the call fails with `EXPECTATION NOT MET` plus a screenshot.

!!! Tip "Coordinates"

    `click`, `drag` and `scroll` take pixel positions of the **most recent screenshot** by default, so an AI can click exactly where it sees something, even if the screenshot was scaled down. `coordinate_space` also accepts `window_pixels`, `logical` and `normalized` (0 to 1).

!!! Note "Mouse and keys while streaming"

    While a stream is running and the stream menu is closed, mouse and keyboard input behave like yours: keys are mapped to controller buttons and the mouse controls the touchpad (if enabled). Use `controller_input` to play, and `press_key` with `ctrl+o` to open the stream menu.
