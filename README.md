# Tidal-BB10

An unofficial TIDAL client for BlackBerry 10 phones. It's a native Cascades
app that talks to TIDAL directly: no server, computer or bridge needed.
Tested on a BlackBerry Classic with OS 10.3.

You need a paid TIDAL subscription. This project is not affiliated with,
endorsed by or supported by TIDAL. "TIDAL" is a trademark of its owner and is
used here only to say what the app works with.

## Features

- **Home**: your personal TIDAL home page with daily mixes, radio stations,
  recently played, favourite artists and new albums
- **Search**: tracks, albums, artists and playlists
- **My music**: your mixes, playlists, favourite tracks, albums and artists
- **Player**: play queue, shuffle, seeking, background playback, and the
  lock screen, media keys and headset buttons
- **Long-press a song** to go to its album or artist, add it to a playlist or
  My tracks, or download it. On the Playing screen, tap the artist or album.
- **Playlists**: create playlists and add songs, whole albums or playlists
- **Offline listening**: download songs, albums and playlists to the phone or
  the SD card. Like TIDAL's own offline mode, downloads are encrypted so they
  only play in this app, and only while it has confirmed your subscription in
  the last 30 days.

## Install

1. Download `tidal-bb10.bar` from the [Releases](../../releases) page, or
   build it yourself (see below).
2. On the phone, turn on Development Mode: Settings → Security and Privacy →
   Development Mode.
3. Sideload the `.bar` with a tool such as Sachesi or DDPB. When BB10 asks
   for permissions, allow **Shared Files** if you want to save downloads on
   the SD card.

## First start: log in to TIDAL

The app opens a **Log in to TIDAL** screen with an address and a code.

1. On any other phone or computer with a modern browser, open the address,
   for example `link.tidal.com/ABCDE`. The BB10 browser is too old for
   TIDAL's login page.
2. Log in with your TIDAL account, and enter the code if it asks for one.
3. The app continues by itself within a few seconds.

You never type your TIDAL password into the app: TIDAL's own website asks for
it, the same way you connect a smart TV. The login is saved only on the
phone, in the app's private storage, and renews itself. Nothing about your
account is in the app package. If a code expires, the app fetches a new one;
**New code** does the same by hand.

## Settings

Swipe down from the top of the app → Settings.

| Setting | What it does |
| --- | --- |
| TIDAL account | Log out, or log in again (for example to switch accounts) |
| Streaming quality | Data saver (96k AAC) or Normal (320k AAC); also used for new downloads |
| Save downloads on | Phone or SD card. Move earlier downloads from My music → Downloads. |
| Advanced | The app's identifier at TIDAL (client ID and secret, not your account). Only change it if logging in stops working; clear both fields to go back to the default. |

Swipe down → **Log** shows what the app is doing, including a connection test
of TIDAL's servers on every start. It's the first place to look when
something goes wrong.

## Troubleshooting

- **Nothing loads, or "No connection to TIDAL"**: check the Log. The first
  lines should read `TLS: library OpenSSL 1.1.1w`, followed by
  `Connection test: … OK` for each TIDAL server. If the Log says
  `BB10 system OpenSSL` instead, the bundled network library didn't load;
  reinstall the app.
- **Logging in stops working for everyone**: TIDAL may have blocked the
  app's identifier. Check this repository for an update, or enter a newer
  client ID and secret under Settings → Advanced.
- **Downloads on the SD card are missing**: they're hidden while the card is
  out, and come back when it's inserted again.
- Lines in the Log such as `pushPage`, `NavigationPane` or
  `UIObjectPrivate::notifyMessage` come from BB10 itself and can be ignored.

## Build it yourself

The build runs in a Docker image that contains the BlackBerry 10 Native SDK
10.3.1, because BlackBerry no longer offers the SDK for download. You need
Docker on an x86_64 (Intel/AMD) Linux machine.

```bash
./build.sh                       # Docker on this machine
./remote-build.sh user@server    # or build on another machine over SSH
```

The result is `build/tidal-bb10.bar`, an unsigned package for sideloading.

The first build downloads `lib/libQtNetwork.so.4` (about 4 MB) from the
[bb10-qtnetwork](https://github.com/JBraddockm/bb10-qtnetwork) project with
`tools/fetch-qtnetwork.sh`. The download is pinned to one commit and checked
against its SHA-256. It isn't stored in this repository, because that project
doesn't have a licence yet (see Credits).

`tools/make-icons.py` redraws the app icon and button images.

## How it works

BB10's own networking only speaks TLS 1.0 and trusts certificates from around
2017. TIDAL's servers require TLS 1.2 or newer, so the stock system can't
reach them. The app works around that, and around BB10's old media player:

| Part | What it does |
| --- | --- |
| `lib/libQtNetwork.so.4` | A replacement QtNetwork with OpenSSL 1.1.1 (TLS 1.2 and 1.3) built in. The app starts with `app/native/lib` first on its library path (`bar-descriptor.xml`), so this copy loads instead of the system one. |
| `assets/cacert.pem` | Current Mozilla root certificates, used for every secure connection |
| `assets/tidal.js` | The TIDAL client: login, renewing the login, browsing, playlists and favourites |
| `src/localserver.cpp` | A small server on `127.0.0.1` inside the app. The media player streams from it: it relays songs from TIDAL and decrypts downloads as they play. |
| `src/downloads.cpp` | Downloads, encrypts and keeps track of songs for offline listening |
| `src/player.cpp` | The play queue, shuffle, and the system's "now playing" controls |

## Security and limitations

- Your TIDAL login stays on your phone. Anyone who can use the unlocked phone
  can use the app, as with any music app.
- The app carries the same TIDAL app identifier as the tidalapi library. It
  identifies the app, not you, but anyone can read it from the `.bar`, and
  TIDAL can block it at any time.
- The app uses TIDAL's unofficial API, which can change at any time and
  break the app until it's updated. Using third-party clients may go against
  TIDAL's terms of service.
- Uninstalling the app makes downloads on the SD card unplayable. Delete the
  hidden `.tidal-bb10` folder on the card to free the space.
- Videos, Dolby Atmos and lossless are not supported.

## Credits

This app stands on the work of others. Thank you to:

- **[JBraddockm/bb10-qtnetwork](https://github.com/JBraddockm/bb10-qtnetwork)**
  for the patched Qt 4.8.6 QtNetwork with OpenSSL 1.1.1w that gives BB10
  modern TLS. Without it, this app could not reach TIDAL. That project in
  turn credits:
  - **[sw7ft/BerryCore](https://github.com/sw7ft/BerryCore)** for the
    OpenSSL 1.1.1w build for BB10
  - University of Melbourne patches for porting Qt 4.8 to OpenSSL 1.1
- **[EbbLabs/python-tidal](https://github.com/EbbLabs/python-tidal)**
  (tidalapi, originally by tamland, and its contributors) for showing how
  TIDAL's API, login and streams work. This app's TIDAL client is a fresh
  JavaScript implementation based on that knowledge, and uses the same public
  app identifier.
- **[The OpenSSL Project](https://www.openssl.org/)**: OpenSSL is built into
  the QtNetwork library above. *This product includes software developed by
  the OpenSSL Project for use in the OpenSSL Toolkit
  (https://www.openssl.org/). This product includes cryptographic software
  written by Eric Young (eay@cryptsoft.com).*
- **[Mozilla](https://wiki.mozilla.org/CA)** for the root certificate list,
  and **[curl](https://curl.se/docs/caextract.html)** for publishing it as
  `cacert.pem`
- **[The Qt Project](https://www.qt.io/)** for Qt 4.8, and **BlackBerry** for
  Cascades and the BlackBerry 10 Native SDK
- **[yamsergey/bb10-ndk](https://hub.docker.com/r/yamsergey/bb10-ndk)** for
  the Docker image with the BB10 Native SDK, which makes building possible
  today
- **[zsavajji/berry-cascades](https://github.com/zsavajji/berry-cascades)**,
  whose build setup showed how to build and package a Cascades app from the
  command line with that image
- The BB10 community, for keeping these phones useful

### Third-party licences

| Component | Used as | Licence |
| --- | --- | --- |
| bb10-qtnetwork (`lib/libQtNetwork.so.4`) | Downloaded at build time, included in the `.bar` | No licence published yet. It's based on Qt 4.8.6 (LGPL 2.1) and OpenSSL 1.1.1w (OpenSSL and SSLeay licence). |
| Mozilla root certificates (`assets/cacert.pem`) | Included | [MPL 2.0](https://www.mozilla.org/MPL/2.0/) |
| tidalapi | Reference only; no code included | LGPL-3.0 |
| Qt 4.8, Cascades | Provided by the phone | LGPL 2.1 / BlackBerry |
| BlackBerry 10 Native SDK, `yamsergey/bb10-ndk` | Build only | BlackBerry SDK licence agreement |

**Note for anyone publishing a `.bar`:** the package contains the
bb10-qtnetwork library. Until that project publishes a licence and its source
changes, which Qt's LGPL requires for modified versions, sharing a `.bar` that
includes it isn't clearly permitted. Building it yourself from this repository
for your own phone is fine.

## Licence

Copyright (C) 2026 Tidal-BB10 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. See [LICENSE](LICENSE) for the full text.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.

The GPL applies to this project's own code. Third-party components keep their
own licences (see above).
