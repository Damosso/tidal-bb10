// TIDAL client: talks to TIDAL directly from the phone.
//
// route(path, cb) answers the same page addresses the screens used to ask
// the bridge server for ("/api/album/123", "/api/home", ...) with the same
// JSON shape: { title, subtitle, sections: [ { title, items: [...] } ], next }.
// Callbacks are cb(error, result); error "needLogin" means the login prompt
// should be shown.
//
// Uses the context properties `app` (login storage, see app.cpp).

var API1 = "https://api.tidal.com/v1/";
var API2 = "https://api.tidal.com/v2/";
var AUTH = "https://auth.tidal.com/v1/oauth2/";
var SCOPE = "r_usr w_usr w_sub";
var CLIENT_VERSION = "2025.7.16";

// ---------------------------------------------------------------- helpers

function enc(obj) {
    var parts = [];
    for (var k in obj) {
        if (obj[k] !== undefined && obj[k] !== null)
            parts.push(encodeURIComponent(k) + "=" + encodeURIComponent("" + obj[k]));
    }
    return parts.join("&");
}

function apiUrl(base, path, params) {
    var q = { "countryCode": app.countryCode != "" ? app.countryCode : "US",
              "locale": "en_US", "deviceType": "BROWSER" };
    if (params) {
        for (var k in params)
            q[k] = params[k];
    }
    return base + path + "?" + enc(q);
}

function cover(uuid, size) {
    if (!uuid)
        return "";
    return "https://resources.tidal.com/images/" + ("" + uuid).replace(/-/g, "/") + "/" + size + "x" + size + ".jpg";
}

function parseQuery(text) {
    var out = {};
    if (!text)
        return out;
    var pairs = text.split("&");
    for (var i = 0; i < pairs.length; i++) {
        var kv = pairs[i].split("=");
        if (kv[0])
            out[decodeURIComponent(kv[0])] = kv.length > 1 ? decodeURIComponent(kv[1].replace(/\+/g, " ")) : "";
    }
    return out;
}

// Runs several requests at once; cb(error, [results in order]).
function all(tasks, cb) {
    var results = [];
    var left = tasks.length;
    var failed = false;
    if (left == 0) {
        cb(null, results);
        return;
    }
    for (var i = 0; i < tasks.length; i++) {
        (function (index) {
            tasks[index](function (err, data) {
                if (failed)
                    return;
                if (err) {
                    failed = true;
                    cb(err);
                    return;
                }
                results[index] = data;
                if (--left == 0)
                    cb(null, results);
            });
        })(i);
    }
}

// ---------------------------------------------------------------- requests

var refreshing = false;
var waitingForRefresh = [];

// Low-level request. cb(error, data, xhr).
function send(method, url, body, headers, cb, retried) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
        if (xhr.readyState != 4)
            return;
        var data = null;
        try {
            data = JSON.parse(xhr.responseText);
        } catch (e) {
        }
        if (xhr.status == 401 && !retried && app.refreshToken != "") {
            // The access token lasts a few hours; renew it and try again.
            refresh(function (ok) {
                if (ok)
                    send(method, url, body, headers, cb, true);
                else
                    cb("needLogin", data, xhr);
            });
            return;
        }
        if (xhr.status >= 200 && xhr.status < 300) {
            cb(null, data, xhr);
            return;
        }
        if (xhr.status == 0) {
            // Qt 4.8's XMLHttpRequest reports a "400 Bad Request" answer as
            // status 0 and drops its text. While waiting for the device login
            // that answer just means "not logged in yet".
            if (url == AUTH + "token" && body && body.indexOf("device_code=") >= 0) {
                cb("pending", data, xhr);
                return;
            }
            console.log("TIDAL: no answer for " + method + " " + url.split("?")[0]
                        + " (no connection, or TIDAL refused the request)");
            cb("No connection to TIDAL, or TIDAL refused the request", data, xhr);
            return;
        }
        var msg = (data && (data.userMessage || data.error_description || data.error))
                  ? (data.userMessage || data.error_description || data.error) : "TIDAL error " + xhr.status;
        if (!(data && data.error == "authorization_pending"))   // normal while logging in
            console.log("TIDAL: " + xhr.status + " for " + method + " " + url.split("?")[0] + ": " + msg);
        cb(xhr.status == 401 ? "needLogin" : msg, data, xhr);
    };
    xhr.open(method, url);
    xhr.setRequestHeader("x-tidal-client-version", CLIENT_VERSION);
    if (app.accessToken != "" && url.indexOf(AUTH) != 0)
        xhr.setRequestHeader("Authorization", "Bearer " + app.accessToken);
    if (headers) {
        for (var h in headers)
            xhr.setRequestHeader(h, headers[h]);
    }
    if (body !== null && body !== undefined) {
        xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
        xhr.send(body);
    } else {
        xhr.send();
    }
}

function refresh(cb) {
    waitingForRefresh.push(cb);
    if (refreshing)
        return;
    refreshing = true;
    var body = enc({ "grant_type": "refresh_token", "refresh_token": app.refreshToken,
                     "client_id": app.clientId, "client_secret": app.clientSecret, "scope": SCOPE });
    send("POST", AUTH + "token", body, null, function (err, data) {
        var ok = !err && data && data.access_token;
        if (ok) {
            app.saveTokens(data.access_token, data.refresh_token ? data.refresh_token : "");
        } else {
            console.log("TIDAL: renewing the login failed: " + err);
            // A rejected refresh token means a new login is needed.
            if (data && (data.error == "invalid_grant" || data.error == "invalid_client"))
                app.logout();
        }
        refreshing = false;
        var waiting = waitingForRefresh;
        waitingForRefresh = [];
        for (var i = 0; i < waiting.length; i++)
            waiting[i](ok);
    }, true);
}

function get(path, params, cb) {
    send("GET", apiUrl(API1, path, params), null, null, cb);
}

function get2(path, params, cb) {
    send("GET", apiUrl(API2, path, params), null, null, cb);
}

function userPath(rest) {
    return "users/" + app.userId + "/" + rest;
}

// ---------------------------------------------------------------- login

// cb(error, { url, code, deviceCode, interval, expiresIn })
function startLogin(cb) {
    var body = enc({ "client_id": app.clientId, "scope": SCOPE });
    send("POST", AUTH + "device_authorization", body, null, function (err, data) {
        if (err || !data || !data.deviceCode) {
            cb(err ? err : "TIDAL did not hand out a login code");
            return;
        }
        var url = data.verificationUriComplete ? data.verificationUriComplete : data.verificationUri;
        if (url.indexOf("http") != 0)
            url = "https://" + url;
        cb(null, { "url": url, "code": data.userCode, "deviceCode": data.deviceCode,
                   "interval": data.interval ? data.interval : 5, "expiresIn": data.expiresIn });
    }, true);
}

// cb(error, state): state is "done", "pending" or "expired".
function pollLogin(deviceCode, cb) {
    var body = enc({ "client_id": app.clientId, "client_secret": app.clientSecret,
                     "device_code": deviceCode, "scope": SCOPE,
                     "grant_type": "urn:ietf:params:oauth:grant-type:device_code" });
    send("POST", AUTH + "token", body, null, function (err, data) {
        if (!err && data && data.access_token) {
            app.saveTokens(data.access_token, data.refresh_token);
            loadAccount(function (err2) {
                cb(err2, err2 ? null : "done");
            });
            return;
        }
        if (err == "pending" || (data && data.error == "authorization_pending")) {
            cb(null, "pending");
            return;
        }
        if (data && data.error == "expired_token") {
            cb(null, "expired");
            return;
        }
        cb(err ? err : "login failed");
    }, true);
}

// Reads user id and country for the saved login.
function loadAccount(cb) {
    send("GET", API1 + "sessions", null, null, function (err, data) {
        if (err || !data || !data.userId) {
            cb(err ? err : "could not read the account");
            return;
        }
        app.saveAccount("" + data.userId, data.countryCode ? data.countryCode : "US");
        cb(null, data);
    });
}

// Confirms the subscription works (for the 30-day offline rule).
function checkSubscription(cb) {
    get(userPath("subscription"), null, function (err, data) {
        cb(err, data);
    });
}

// ---------------------------------------------------------------- streams

// cb(error, url): a signed link to the song's audio file.
function streamUrl(trackId, cb) {
    var quality = app.quality != "" ? app.quality : "HIGH";
    var ask = function (q, retry) {
        get("tracks/" + trackId + "/urlpostpaywall",
            { "urlusagemode": "STREAM", "audioquality": q, "assetpresentation": "FULL" },
            function (err, data) {
                if (!err && data && data.urls && data.urls.length > 0) {
                    cb(null, data.urls[0]);
                } else if (retry && q != "HIGH") {
                    console.log("TIDAL: " + q + " not available, trying HIGH");
                    ask("HIGH", false);
                } else {
                    cb(err ? err : "no stream for this song");
                }
            });
    };
    ask(quality, true);
}

// ---------------------------------------------------------------- converting

function names(list) {
    var out = [];
    for (var i = 0; list && i < list.length; i++) {
        if (list[i] && list[i].name)
            out.push(list[i].name);
    }
    return out.join(", ");
}

function trackRow(t) {
    if (!t || !t.id)
        return null;
    var artists = t.artists ? t.artists : (t.artist ? [t.artist] : []);
    var album = t.album ? t.album : {};
    return {
        "type": "track",
        "id": t.id,
        "title": t.title + (t.version ? " (" + t.version + ")" : ""),
        "artist": names(artists),
        "album": album.title ? album.title : "",
        "albumId": album.id ? album.id : "",
        "artistId": artists.length > 0 && artists[0].id ? artists[0].id : "",
        "imgUrl": cover(album.cover, 160),
        "duration": t.duration ? t.duration : 0,
        "ok": t.allowStreaming !== false && t.streamReady !== false
    };
}

function albumRow(a) {
    if (!a || !a.id)
        return null;
    return {
        "type": "album",
        "id": a.id,
        "title": a.title,
        "artist": names(a.artists ? a.artists : (a.artist ? [a.artist] : [])),
        "imgUrl": cover(a.cover, 160),
        "year": a.releaseDate ? ("" + a.releaseDate).substring(0, 4) : null
    };
}

function artistRow(a) {
    if (!a || !a.id)
        return null;
    return { "type": "artist", "id": a.id, "title": a.name, "imgUrl": cover(a.picture, 160) };
}

function playlistRow(p) {
    if (!p || !p.uuid)
        return null;
    return {
        "type": "playlist",
        "id": p.uuid,
        "title": p.title,
        "artist": (p.numberOfTracks ? p.numberOfTracks : 0) + " tracks",
        "imgUrl": cover(p.squareImage, 160)
    };
}

function mixImage(m) {
    if (m.images) {   // v1: { SMALL: { url } }
        var img = m.images.SMALL ? m.images.SMALL : (m.images.MEDIUM ? m.images.MEDIUM : null);
        return img && img.url ? img.url : "";
    }
    var list = m.mixImages ? m.mixImages : [];   // v2: [ { size, url } ]
    for (var i = 0; i < list.length; i++) {
        if (list[i].size == "SMALL")
            return list[i].url;
    }
    return list.length > 0 ? list[0].url : "";
}

function mixRow(m) {
    if (!m || !m.id)
        return null;
    var title = m.title ? m.title : (m.titleTextInfo ? m.titleTextInfo.text : "Mix");
    var sub = m.subTitle ? m.subTitle : (m.subtitleTextInfo ? m.subtitleTextInfo.text : "");
    return { "type": "mix", "id": m.id, "title": title, "artist": sub, "imgUrl": mixImage(m) };
}

function anyRow(type, data) {
    switch (("" + type).toUpperCase()) {
    case "TRACK": return trackRow(data);
    case "ALBUM": return albumRow(data);
    case "ARTIST": return artistRow(data);
    case "PLAYLIST": return playlistRow(data);
    case "MIX": return mixRow(data);
    }
    return null;
}

function rows(list, convert, wrapped) {
    var out = [];
    for (var i = 0; list && i < list.length; i++) {
        var r = convert(wrapped ? list[i][wrapped] : list[i]);
        if (r)
            out.push(r);
    }
    return out;
}

function page(title, subtitle, sections, next) {
    return { "title": title, "subtitle": subtitle, "sections": sections, "next": next ? next : null };
}

function nextOffset(data, offset, size) {
    return data && data.totalNumberOfItems > offset + size ? offset + size : null;
}

// ---------------------------------------------------------------- pages

function home(cb) {
    get2("home/feed/static", { "platform": "WEB" }, function (err, data) {
        if (err)
            return cb(err);
        var sections = [];
        var list = data && data.items ? data.items : [];
        for (var i = 0; i < list.length; i++) {
            var section = list[i];
            var title = section.title ? section.title : "";
            if (title == "Shortcuts")
                title = "Jump back in";
            var items = [];
            var sub = section.items ? section.items : [];
            for (var j = 0; j < sub.length; j++) {
                var r = anyRow(sub[j].type, sub[j].data);
                if (r)
                    items.push(r);
            }
            // Show songs only where songs are the point of the section.
            var onlyTracks = true;
            for (j = 0; j < items.length; j++) {
                if (items[j].type != "track")
                    onlyTracks = false;
            }
            if (!onlyTracks)
                items = items.filter(function (x) { return x.type != "track"; });
            if (items.length > 0)
                sections.push({ "title": title, "items": items.slice(0, 15) });
        }
        cb(null, page("Home", "", sections));
    });
}

// Old-style TIDAL pages (mixes): rows → modules → pagedList.items
function pageModules(data) {
    var out = [];
    var rowsList = data && data.rows ? data.rows : [];
    for (var i = 0; i < rowsList.length; i++) {
        var mods = rowsList[i].modules ? rowsList[i].modules : [];
        for (var j = 0; j < mods.length; j++)
            out.push(mods[j]);
    }
    return out;
}

function mix(id, cb) {
    get("pages/mix", { "mixId": id }, function (err, data) {
        if (err)
            return cb(err);
        var title = "Mix", subtitle = "Mix", tracks = [];
        var mods = pageModules(data);
        for (var i = 0; i < mods.length; i++) {
            if (mods[i].mix) {
                title = mods[i].mix.title ? mods[i].mix.title : title;
                subtitle = mods[i].mix.subTitle ? mods[i].mix.subTitle : subtitle;
            }
            if (mods[i].type == "TRACK_LIST" && mods[i].pagedList)
                tracks = tracks.concat(rows(mods[i].pagedList.items, trackRow));
        }
        cb(null, page(title, subtitle, [ { "title": "", "items": tracks } ]));
    });
}

function myMixes(cb) {
    get("pages/my_collection_my_mixes", null, function (err, data) {
        if (err)
            return cb(err);
        var items = [];
        var mods = pageModules(data);
        for (var i = 0; i < mods.length; i++) {
            if (mods[i].pagedList)
                items = items.concat(rows(mods[i].pagedList.items, mixRow));
        }
        cb(null, page("My mixes", "", [ { "title": "", "items": items } ]));
    });
}

function search(q, cb) {
    get("search", { "query": q, "types": "TRACKS,ALBUMS,ARTISTS,PLAYLISTS", "limit": 30 }, function (err, d) {
        if (err)
            return cb(err);
        cb(null, page("", "", [
            { "title": "Tracks", "items": rows(d.tracks ? d.tracks.items : [], trackRow) },
            { "title": "Albums", "items": rows(d.albums ? d.albums.items : [], albumRow).slice(0, 12) },
            { "title": "Artists", "items": rows(d.artists ? d.artists.items : [], artistRow).slice(0, 8) },
            { "title": "Playlists", "items": rows(d.playlists ? d.playlists.items : [], playlistRow).slice(0, 12) }
        ]));
    });
}

function album(id, cb) {
    all([
        function (done) { get("albums/" + id, null, done); },
        function (done) { get("albums/" + id + "/tracks", { "limit": 100 }, done); }
    ], function (err, r) {
        if (err)
            return cb(err);
        var a = r[0];
        cb(null, page(a.title, names(a.artists ? a.artists : [a.artist]),
                      [ { "title": "", "items": rows(r[1].items, trackRow) } ]));
    });
}

function artist(id, cb) {
    all([
        function (done) { get("artists/" + id, null, done); },
        function (done) { get("artists/" + id + "/toptracks", { "limit": 20 }, done); },
        function (done) { get("artists/" + id + "/albums", { "limit": 50 }, done); },
        function (done) { get("artists/" + id + "/albums", { "limit": 50, "filter": "EPSANDSINGLES" }, done); }
    ], function (err, r) {
        if (err)
            return cb(err);
        cb(null, page(r[0].name, "Artist", [
            { "title": "Top tracks", "items": rows(r[1].items, trackRow) },
            { "title": "Albums", "items": rows(r[2].items, albumRow) },
            { "title": "EPs & singles", "items": rows(r[3].items, albumRow) }
        ]));
    });
}

function playlist(id, offset, cb) {
    var tasks = [ function (done) { get("playlists/" + id + "/items", { "limit": 100, "offset": offset }, done); } ];
    if (offset == 0)
        tasks.push(function (done) { get("playlists/" + id, null, done); });
    all(tasks, function (err, r) {
        if (err)
            return cb(err);
        var tracks = [];
        var list = r[0].items ? r[0].items : [];
        for (var i = 0; i < list.length; i++) {
            if (list[i].type == "track") {
                var t = trackRow(list[i].item);
                if (t)
                    tracks.push(t);
            }
        }
        var title = r[1] ? r[1].title : "";
        cb(null, page(title, "Playlist", [ { "title": "", "items": tracks } ], nextOffset(r[0], offset, 100)));
    });
}

function myList(what, offset, cb) {
    var spec = {
        "playlists": [ "playlistsAndFavoritePlaylists", 50, playlistRow, "playlist", "My playlists" ],
        "tracks": [ "favorites/tracks", 100, trackRow, "item", "My tracks" ],
        "albums": [ "favorites/albums", 100, albumRow, "item", "My albums" ],
        "artists": [ "favorites/artists", 100, artistRow, "item", "My artists" ]
    }[what];
    if (!spec)
        return cb("unknown list");
    var params = { "limit": spec[1], "offset": offset };
    if (what != "playlists") {
        params.order = "DATE";
        params.orderDirection = "DESC";
    }
    get(userPath(spec[0]), params, function (err, data) {
        if (err)
            return cb(err);
        cb(null, page(spec[4], "", [ { "title": "", "items": rows(data.items, spec[2], spec[3]) } ],
                      nextOffset(data, offset, spec[1])));
    });
}

// Playlists you created (for "Add to playlist").
function ownPlaylists(cb) {
    var found = [];
    var step = function (offset) {
        get(userPath("playlists"), { "limit": 50, "offset": offset }, function (err, data) {
            if (err)
                return cb(err);
            found = found.concat(rows(data.items, playlistRow));
            var next = nextOffset(data, offset, 50);
            if (next !== null && next < 500)
                step(next);
            else
                cb(null, page("Your playlists", "", [ { "title": "", "items": found } ]));
        });
    };
    step(0);
}

// Answers the page addresses the screens ask for.
function route(path, cb) {
    if (!app.loggedIn)
        return cb("needLogin");
    var split = path.split("?");
    var parts = split[0].split("/");   // "", "api", kind, id
    var q = parseQuery(split[1]);
    var kind = parts[2], id = parts[3];
    var offset = q.offset ? parseInt(q.offset, 10) : 0;
    switch (kind) {
    case "home": return home(cb);
    case "search": return search(q.q ? q.q : "", cb);
    case "album": return album(id, cb);
    case "artist": return artist(id, cb);
    case "playlist": return playlist(id, offset, cb);
    case "mix": return mix(id, cb);
    case "my":
        if (id == "mixes")
            return myMixes(cb);
        if (id == "own-playlists")
            return ownPlaylists(cb);
        return myList(id, offset, cb);
    }
    cb("unknown page " + path);
}

// ---------------------------------------------------------------- editing

function addToPlaylist(uuid, ids, cb) {
    // Read the playlist first: its version tag (ETag) and length are needed.
    get("playlists/" + uuid, null, function (err, p, xhr) {
        if (err)
            return cb(err);
        if (!p.creator || "" + p.creator.id != app.userId)
            return cb("You can only add to playlists you created");
        var etag = xhr.getResponseHeader("ETag");
        if (!etag)
            etag = xhr.getResponseHeader("etag");
        var body = enc({ "onArtifactNotFound": "SKIP", "trackIds": ids, "toIndex": p.numberOfTracks,
                         "onDupes": "SKIP" });
        send("POST", apiUrl(API1, "playlists/" + uuid + "/items", null), body,
             etag ? { "If-None-Match": etag } : null, function (err2) {
                 cb(err2, err2 ? null : { "ok": true, "title": p.title });
             });
    });
}

// Write actions; cb(error, { ok: true, ... }).
function post(path, cb) {
    if (!app.loggedIn)
        return cb("needLogin");
    var split = path.split("?");
    var parts = split[0].split("/");   // "", "api", ...
    var q = parseQuery(split[1]);
    var done = function (err) { cb(err, err ? null : { "ok": true }); };

    if (parts[2] == "favorite") {
        var kind = parts[3], id = parts[4];
        if (kind == "playlist") {
            send("PUT", apiUrl(API2, "my-collection/playlists/folders/add-favorites",
                               { "folderId": "root", "uuids": id }), null, null, done);
            return;
        }
        var field = { "track": "trackId", "album": "albumId", "artist": "artistId" }[kind];
        if (!field)
            return cb("unknown kind");
        if (q.remove == "1") {
            send("DELETE", apiUrl(API1, userPath("favorites/" + kind + "s/" + id), null), null, null, done);
        } else {
            var form = {};
            form[field] = id;
            send("POST", apiUrl(API1, userPath("favorites/" + kind + "s"), null), enc(form), null, done);
        }
        return;
    }
    if (parts[2] == "playlist" && parts[4] == "add") {
        addToPlaylist(parts[3], q.tracks, cb);
        return;
    }
    if (parts[2] == "playlists" && parts[3] == "new") {
        send("PUT", apiUrl(API2, "my-collection/playlists/folders/create-playlist",
                           { "name": q.title, "description": "", "folderId": "root" }), null, null,
             function (err, data) {
                 if (err)
                     return cb(err);
                 var uuid = data && data.data ? data.data.uuid : null;
                 if (!uuid)
                     return cb("TIDAL did not create the playlist");
                 if (q.tracks)
                     addToPlaylist(uuid, q.tracks, cb);
                 else
                     cb(null, { "ok": true });
             });
        return;
    }
    cb("unknown action " + path);
}
