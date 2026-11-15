# Tidal-BB10 — TIDAL on BlackBerry 10

An unofficial TIDAL client for BlackBerry 10 phones (tested on a BlackBerry
Classic with OS 10.3). It has two parts:

- **The bridge** (`server/`): a small Python server you run on a computer,
  NAS or VPS. It logs in to TIDAL with your own account and relays music and
  cover art to the phone over plain HTTP, which the old BB10 network stack can
  handle.
- **The app** (`app/`): a native Cascades app for the phone, plus a
  lightweight web player served by the bridge for use in the BB10 browser.

```
BlackBerry 10  ──HTTP──▶  Tidal-BB10 bridge  ──HTTPS──▶  TIDAL
 (app or browser)         (your server)
```

You need a paid TIDAL subscription. The bridge uses the unofficial
[tidalapi](https://github.com/tamland/python-tidal) library. This project is
not affiliated with or endorsed by TIDAL.

## Features

- Home screen with your TIDAL home page: daily mixes, radio stations,
  recently played, favourite artists, new albums
- Search tracks, albums, artists and playlists
- Your library: mixes, playlists, favourite tracks, albums and artists
- Play queue with shuffle, seeking, lock-screen and headset-button controls
- Background playback
- Optional password protection for servers reachable from the internet

## 1. Run the bridge

Pick one of the three ways below. Afterwards, open
`http://<server-address>:8686/` in any browser: the first time it shows a
code to enter at link.tidal.com to connect your TIDAL account. That login is
saved, so you only do it once.

### Portainer

1. Stacks → Add stack → **Repository**.
2. Repository URL: this repository. Compose path: `docker-compose.yml`.
3. Under **Environment variables** add `BB_PASSWORD` with a long random
   password (and optionally `TIDAL_QUALITY`, see below).
4. Deploy the stack.

To update later, use **Pull and redeploy** on the stack page.

### Docker Compose

```bash
git clone <this repository> tidal-bb10
cd tidal-bb10
cp .env.example .env     # then set BB_PASSWORD in .env
docker compose up -d
```

### Plain Python (home network only)

Needs Python 3.10 or newer.

```bash
cd server
./start.sh
```

It prints the address to use on the phone.

### Settings

| Variable | Meaning |
| --- | --- |
| `BB_PASSWORD` | Password the app and web player must send. User name in the browser: `bb`. Required in the Docker setup. |
| `TIDAL_QUALITY` | `LOW` (96k AAC, saves mobile data), `HIGH` (320k AAC, default), `LOSSLESS` (FLAC, needs a lot of bandwidth) |
| `PORT` | Port on the host (Docker) or to listen on (plain Python). Default `8686`. |
| `TIDAL_SESSION_FILE` | Where the TIDAL login is saved. The Docker image uses a volume. |

## 2. Install the app on the phone

1. Download `tidal-bb10.bar` from the
   [Releases](../../releases) page, or build it yourself (below).
2. On the phone, turn on Development Mode: Settings → Security and Privacy →
   Development Mode.
3. Sideload the `.bar` with a tool such as Sachesi or DDPB.
4. Open Tidal-BB10. It asks for the server address (for example
   `192.168.1.10:8686`) and the password. You can change these later by
   swiping down from the top of the app → Settings.

Prefer not to install anything? Open `http://<server-address>:8686/` in the
BB10 browser instead. The web player keeps playing with the screen off.

### Troubleshooting

- **"Cannot reach the server"**: check the address, that the phone can reach
  that machine (same Wi-Fi, or the port is open in the server's firewall), and
  that the bridge is running.
- **"The server wants a password"**: enter the same `BB_PASSWORD` in Settings.
- **Anything else**: swipe down from the top → Log shows what went wrong.

## 3. Build the app yourself

BlackBerry no longer offers the Native SDK for download, so the build runs in
a community Docker image that contains the BlackBerry 10 Native SDK 10.3.1
(`yamsergey/bb10-ndk:0.6`). It needs Docker on an x86_64 (Intel/AMD) Linux
machine.

```bash
cd app
./build.sh                       # with Docker on this machine
./remote-build.sh user@server    # or: build on another machine over SSH
```

The result is `app/build/tidal-bb10.bar`, an unsigned package for sideloading.
`app/tools/make-icons.py` redraws the icon and button images.

## Security and limitations

- The phone talks to the bridge over **plain HTTP**: the password and the
  music are not encrypted on the way. Use a password you use nowhere else,
  or keep the bridge on your home network or behind a VPN.
- Anyone with the password can play music on your TIDAL account, within
  TIDAL's limits for simultaneous streams.
- The saved login (`tidal-session.json`, or the Docker volume) gives access
  to your TIDAL account. Never share or commit it.
- TIDAL can change its private API at any time. When something breaks,
  updating `tidalapi` usually fixes it: redeploy the stack, or run
  `.venv/bin/pip install -U tidalapi` in `server/`.
- Videos and Dolby Atmos tracks are not supported.

## Credits

- [tidalapi](https://github.com/tamland/python-tidal) for talking to TIDAL
- The BB10 community for keeping the Native SDK usable, including the
  `yamsergey/bb10-ndk` Docker image

## License

Copyright (C) 2026 Tidal-BB10 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. See [LICENSE](LICENSE) for the full text.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.
