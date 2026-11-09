# Room Studio

Room Studio is a local browser-based helper for creating and editing room data. It is not a hosted online service and should not be exposed to a public network.

## Start the editor

From the engine repository root, run:

```sh
python3 tools/room_editor_server.py --project-root /path/to/my-game
```

Then open `http://127.0.0.1:8765/tools/room-editor.html` in a browser. The editor reads the selected project's room files and texture assets and uses a validated local API to save room changes.

If the default browser does not open automatically, entering the URL directly is sufficient. Stop the server with Ctrl+C when finished.

## Safety and project ownership

- The server is intended for use by the local developer only.
- It binds to loopback (`127.0.0.1`), not a public network interface.
- It writes into the selected game project's `rooms/` and reads its texture/collision data.
- Back up project data before editing rooms that contain important work.
- The Room Studio HTML can be published as static documentation only if copied separately; the Python server and its write API must not be deployed as a public web service.
