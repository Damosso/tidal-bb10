#!/usr/bin/env python3
"""Tidal-BB10 bridge: TIDAL for BlackBerry 10.

Runs on a computer on your network, logs in to Tidal with your account, and
serves a lightweight web player. All audio and artwork is proxied over plain
HTTP so the old BB10 browser never has to talk to Tidal's servers itself.

Settings (environment variables):
  TIDAL_QUALITY   LOW (96k AAC) | HIGH (320k AAC, default) | LOSSLESS (FLAC)
  PORT            port to listen on (default 8686)
  BB_PASSWORD     if set, the player asks for this password (user name: bb)
  TIDAL_SESSION_FILE  where the TIDAL login is saved (default: next to this file)
"""
import os
import re
import threading
import time
from pathlib import Path

import requests
import tidalapi
from flask import Flask, Response, jsonify, request, send_from_directory

HERE = Path(__file__).resolve().parent
SESSION_FILE = Path(os.environ.get("TIDAL_SESSION_FILE") or HERE / "tidal-session.json")
PORT = int(os.environ.get("PORT", "8686"))
PASSWORD = os.environ.get("BB_PASSWORD", "")
QUALITY = os.environ.get("TIDAL_QUALITY", "HIGH").upper()

app = Flask(__name__, static_folder=None)
session = tidalapi.Session()
session.audio_quality = tidalapi.Quality(QUALITY)
http = requests.Session()

login_lock = threading.Lock()
pending_login = None  # {"url":..., "code":..., "expires":...}
url_cache = {}  # track id -> (url, fetched_at)
URL_TTL = 120
cover_cache = {}  # (uuid, size) -> jpeg bytes


# ---------- login ----------

def logged_in():
    try:
        return session.check_login()
    except Exception:
        return False


def has_session():
    """Cheap local check (no network) that a TIDAL login is loaded."""
    u = session.user
    return bool(u is not None and getattr(u, "id", None) and session.session_id)


def login_keeper():
    """While not logged in, keep a valid login code available and print it,
    so it can be read from the server or container log."""
    while not has_session():
        p = pending_login
        if not p or p["expires"] <= time.time():
            try:
                start_login()
            except Exception as e:
                print("Could not start TIDAL login:", e)
        time.sleep(5)


def try_restore():
    if SESSION_FILE.exists():
        try:
            session.load_session_from_file(SESSION_FILE)
        except Exception as e:
            print("Could not restore saved session:", e)


def start_login():
    """Start Tidal's device login and return the link the user must open."""
    global pending_login
    with login_lock:
        if pending_login and pending_login["expires"] > time.time():
            return pending_login
        link, future = session.login_oauth()
        url = link.verification_uri_complete
        if not url.startswith("http"):
            url = "https://" + url
        pending_login = {
            "url": url,
            "code": link.user_code,
            "expires": time.time() + link.expires_in,
        }

        def done(f):
            global pending_login
            pending_login = None
            if f.exception() is None and logged_in():
                session.save_session_to_file(SESSION_FILE)
                print("Logged in to Tidal.")
            else:
                print("Tidal login failed or expired:", f.exception())

        future.add_done_callback(done)
        print("\n=== Connect your TIDAL account ===\n"
              "On any phone or computer, open  %s\n"
              "(or go to link.tidal.com and enter the code  %s )\n"
              "The code expires in %d minutes; a new one is printed after that.\n"
              % (url, link.user_code, link.expires_in // 60), flush=True)
        return pending_login


# ---------- access control ----------

@app.before_request
def check_password():
    if not PASSWORD:
        return None
    auth = request.authorization
    if auth and auth.password == PASSWORD:
        return None
    if request.args.get("k") == PASSWORD:  # the native app sends it this way
        return None
    return Response("Password required", 401,
                    {"WWW-Authenticate": 'Basic realm="Tidal-BB10"'})


LOGIN_FREE = ("/api/status", "/api/login")


@app.before_request
def require_tidal_login():
    if request.path in LOGIN_FREE:
        return None
    if request.path.startswith(("/api/", "/stream/")) and not has_session():
        return jsonify({"error": "Not connected to TIDAL yet", "needLogin": True}), 403
    return None


saved_token = None


@app.after_request
def keep_session_saved(response):
    """Tidal access tokens are renewed automatically every few hours; write
    the renewed login to disk so a restart does not need a new login."""
    global saved_token
    token = session.access_token
    if token and token != saved_token:
        try:
            if saved_token is not None:
                session.save_session_to_file(SESSION_FILE)
            saved_token = token
        except Exception as e:
            print("Could not save renewed session:", e)
    return response


@app.errorhandler(Exception)
def on_error(e):
    code = getattr(e, "code", 500)
    if not isinstance(code, int):
        code = 500
    if code >= 500:
        app.logger.exception("request failed")
    return jsonify({"error": str(e) or e.__class__.__name__}), code


# ---------- JSON shapes ----------

def artist_names(obj):
    artists = getattr(obj, "artists", None) or []
    names = [a.name for a in artists if getattr(a, "name", None)]
    if not names and getattr(obj, "artist", None) is not None:
        names = [obj.artist.name]
    return ", ".join(names)


def track_json(t):
    album = getattr(t, "album", None)
    return {
        "type": "track",
        "id": t.id,
        "title": t.full_name if getattr(t, "full_name", None) else t.name,
        "artist": artist_names(t),
        "album": album.name if album else "",
        "albumId": album.id if album else None,
        "img": getattr(album, "cover", None) if album else None,
        "duration": t.duration or 0,
        "ok": bool(getattr(t, "available", True)),
    }


def album_json(a):
    return {
        "type": "album",
        "id": a.id,
        "title": a.name,
        "artist": artist_names(a),
        "img": getattr(a, "cover", None),
        "year": a.year if getattr(a, "year", None) else None,
    }


def artist_json(a):
    return {"type": "artist", "id": a.id, "title": a.name,
            "img": getattr(a, "picture", None)}


def playlist_json(p):
    return {
        "type": "playlist",
        "id": p.id,
        "title": p.name,
        "artist": "%s tracks" % (getattr(p, "num_tracks", None) or 0),
        "img": getattr(p, "square_picture", None),
    }


def only_tracks(items):
    return [track_json(t) for t in items if isinstance(t, tidalapi.Track)]


MIX_ID = re.compile(r"[0-9a-fA-F]{10,64}")
mix_images = {}  # mix id -> picture URL (mix pictures are not plain uuids)


def mix_json(m):
    img = None
    try:
        url = m.image(320)
        if url and MIX_ID.fullmatch(str(m.id)):
            mix_images[str(m.id)] = url
            img = "m-%s" % m.id
    except Exception:
        pass
    return {"type": "mix", "id": m.id, "title": m.title or "Mix",
            "artist": getattr(m, "sub_title", None) or "", "img": img}


def any_json(item):
    """JSON for anything on a Tidal page; None for things the player can't show."""
    if isinstance(item, tidalapi.Track):
        return track_json(item)
    if isinstance(item, tidalapi.Album):
        return album_json(item)
    if isinstance(item, tidalapi.Artist):
        return artist_json(item)
    if isinstance(item, tidalapi.Playlist):
        return playlist_json(item)
    if isinstance(item, (tidalapi.Mix, tidalapi.MixV2)):
        return mix_json(item)
    return None


home_cache = {"at": 0, "data": None}
HOME_TTL = 300


def build_home():
    sections = []
    for cat in session.home().categories or []:
        title = getattr(cat, "title", None) or ""
        if title == "Shortcuts":
            title = "Jump back in"
        items = [j for j in (any_json(i) for i in (getattr(cat, "items", None) or [])) if j]
        if not items:
            continue
        # Show tracks only where tracks are the point of the section.
        if any(j["type"] != "track" for j in items):
            items = [j for j in items if j["type"] != "track"]
        sections.append({"title": title, "items": items[:15]})
    return {"title": "Home", "sections": sections}


# ---------- API ----------

@app.route("/api/status")
def api_status():
    if logged_in():
        return jsonify({"loggedIn": True, "quality": QUALITY})
    out = {"loggedIn": False}
    if pending_login and pending_login["expires"] > time.time():
        out["login"] = {"url": pending_login["url"], "code": pending_login["code"]}
    return jsonify(out)


@app.route("/api/login", methods=["POST"])
def api_login():
    if logged_in():
        return jsonify({"loggedIn": True})
    p = start_login()
    return jsonify({"loggedIn": False, "login": {"url": p["url"], "code": p["code"]}})


@app.route("/api/search")
def api_search():
    q = request.args.get("q", "").strip()
    if not q:
        return jsonify({"sections": []})
    r = session.search(
        q, models=[tidalapi.Track, tidalapi.Album, tidalapi.Artist, tidalapi.Playlist],
        limit=30)
    return jsonify({"sections": [
        {"title": "Tracks", "items": [track_json(t) for t in r.get("tracks", [])]},
        {"title": "Albums", "items": [album_json(a) for a in r.get("albums", [])[:12]]},
        {"title": "Artists", "items": [artist_json(a) for a in r.get("artists", [])[:8]]},
        {"title": "Playlists", "items": [playlist_json(p) for p in r.get("playlists", [])[:12]]},
    ]})


@app.route("/api/home")
def api_home():
    fresh = request.args.get("fresh") == "1"
    if fresh or not home_cache["data"] or time.time() - home_cache["at"] > HOME_TTL:
        home_cache["data"] = build_home()
        home_cache["at"] = time.time()
    return jsonify(home_cache["data"])


@app.route("/api/mix/<mix_id>")
def api_mix(mix_id):
    if not MIX_ID.fullmatch(mix_id):
        return jsonify({"error": "bad mix id"}), 400
    m = session.mix(mix_id)
    return jsonify({"title": m.title or "Mix", "subtitle": getattr(m, "sub_title", None) or "Mix",
                    "sections": [{"title": "", "items": only_tracks(m.items())}]})


@app.route("/api/album/<int:album_id>")
def api_album(album_id):
    a = session.album(album_id)
    return jsonify({"title": a.name, "subtitle": artist_names(a), "sections": [
        {"title": "", "items": [track_json(t) for t in a.tracks()]}]})


@app.route("/api/artist/<int:artist_id>")
def api_artist(artist_id):
    a = session.artist(artist_id)
    return jsonify({"title": a.name, "subtitle": "Artist", "sections": [
        {"title": "Top tracks", "items": [track_json(t) for t in a.get_top_tracks(limit=20)]},
        {"title": "Albums", "items": [album_json(x) for x in a.get_albums(limit=50)]},
        {"title": "EPs & singles",
         "items": [album_json(x) for x in a.get_albums_ep_singles(limit=50)]},
    ]})


@app.route("/api/playlist/<playlist_id>")
def api_playlist(playlist_id):
    if not re.fullmatch(r"[0-9a-fA-F-]{36}", playlist_id):
        return jsonify({"error": "bad playlist id"}), 400
    p = session.playlist(playlist_id)
    tracks = []
    offset = 0
    while offset < 2000:
        page = p.tracks(limit=100, offset=offset)
        tracks.extend(page)
        if len(page) < 100:
            break
        offset += 100
    return jsonify({"title": p.name, "subtitle": "Playlist", "sections": [
        {"title": "", "items": only_tracks(tracks)}]})


@app.route("/api/my/<what>")
def api_my(what):
    user = session.user
    if what == "playlists":
        items, offset = [], 0
        while offset < 500:
            page = user.playlist_and_favorite_playlists(offset=offset, limit=50)
            items.extend(page)
            if len(page) < 50:
                break
            offset += 50
        return jsonify({"title": "My playlists", "sections": [
            {"title": "", "items": [playlist_json(p) for p in items]}]})
    if what == "mixes":
        items = []
        for cat in session.mixes().categories or []:
            items.extend(getattr(cat, "items", None) or [])
        return jsonify({"title": "My mixes", "sections": [
            {"title": "", "items": [j for j in (any_json(i) for i in items) if j]}]})
    if what == "tracks":
        offset = int(request.args.get("offset", "0"))
        items = user.favorites.tracks(limit=100, offset=offset)
        return jsonify({"title": "My tracks", "sections": [
            {"title": "", "items": only_tracks(items)}],
            "next": offset + 100 if len(items) == 100 else None})
    if what == "albums":
        items = user.favorites.albums(limit=100)
        return jsonify({"title": "My albums", "sections": [
            {"title": "", "items": [album_json(a) for a in items]}]})
    if what == "artists":
        items = user.favorites.artists(limit=100)
        return jsonify({"title": "My artists", "sections": [
            {"title": "", "items": [artist_json(a) for a in items]}]})
    return jsonify({"error": "unknown"}), 404


# ---------- media proxies ----------

def track_url(track_id, fresh=False):
    hit = url_cache.get(track_id)
    if hit and not fresh and time.time() - hit[1] < URL_TTL:
        return hit[0]
    track = tidalapi.Track(session)  # no id: skips the metadata lookup
    track.id = track_id
    url = track.get_url()
    url_cache[track_id] = (url, time.time())
    if len(url_cache) > 200:
        for k in sorted(url_cache, key=lambda k: url_cache[k][1])[:100]:
            url_cache.pop(k, None)
    return url


@app.route("/stream/<int:track_id>")
def stream(track_id):
    headers = {}
    if request.headers.get("Range"):
        headers["Range"] = request.headers["Range"]
    up = http.get(track_url(track_id), headers=headers, stream=True, timeout=20)
    if up.status_code in (401, 403, 404, 410):  # signed URL expired
        up.close()
        up = http.get(track_url(track_id, fresh=True), headers=headers,
                      stream=True, timeout=20)
    out = {"Accept-Ranges": "bytes", "Cache-Control": "no-store"}
    for h in ("Content-Type", "Content-Length", "Content-Range"):
        if h in up.headers:
            out[h] = up.headers[h]
    ctype = out.get("Content-Type", "")
    if "octet-stream" in ctype or not ctype:
        out["Content-Type"] = "audio/flac" if QUALITY == "LOSSLESS" else "audio/mp4"

    def body():
        try:
            for chunk in up.iter_content(64 * 1024):
                yield chunk
        finally:
            up.close()

    return Response(body(), status=up.status_code, headers=out)


@app.route("/cover/<uuid>")
def cover(uuid):
    if uuid.startswith("m-") and uuid[2:] in mix_images:
        size = "mix"
        url = mix_images[uuid[2:]]
    elif re.fullmatch(r"[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", uuid):
        size = "320x320" if request.args.get("s") == "l" else "160x160"
        url = "https://resources.tidal.com/images/%s/%s.jpg" % (uuid.replace("-", "/"), size)
    else:
        return "", 404
    key = (uuid, size)
    data = cover_cache.get(key)
    if data is None:
        up = http.get(url, timeout=15)
        if up.status_code != 200:
            return "", 404
        data = up.content
        if len(cover_cache) > 2000:
            cover_cache.clear()
        cover_cache[key] = data
    return Response(data, headers={
        "Content-Type": "image/jpeg",
        "Cache-Control": "public, max-age=604800"})


@app.route("/")
def index():
    return send_from_directory(HERE / "web", "index.html", max_age=0)


def lan_ip():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "localhost"
    finally:
        s.close()


if __name__ == "__main__":
    try_restore()
    print("\nTidal-BB10 bridge — open on the BlackBerry:  http://%s:%d/\n" % (lan_ip(), PORT))
    if not logged_in():
        threading.Thread(target=login_keeper, daemon=True).start()
    app.run(host="0.0.0.0", port=PORT, threaded=True)
