import bb.cascades 1.2
import tidal 1.0
import bb.system 1.2
import "tidal.js" as Tidal

TabbedPane {
    id: tabs
    showTabsOnActionBar: true

    function openItem(nav, kind, itemId, title) {
        var page = listPageDef.createObject();
        page.main = tabs;
        page.nav = nav;
        page.titleText = title;
        page.kind = kind;
        page.itemId = itemId;
        nav.push(page);
        page.load("/api/" + kind + "/" + itemId);
    }

    property bool loginOpen: false
    property string loginDevice: ""
    property double loginExpires: 0   // ms since 1970; old Qt hides TIDAL's "expired" answer

    // First start: TIDAL hands out a code that you enter at link.tidal.com
    // on any other device. The login is then saved on this phone only.
    function checkLogin() {
        if (!app.loggedIn) {
            tabs.startLogin();
            return;
        }
        Tidal.checkSubscription(function (err) {
            if (!err)
                downloads.markVerified();   // for the 30-day offline rule
            else if (err == "needLogin")
                tabs.startLogin();
        });
    }

    function startLogin() {
        if (tabs.loginOpen && tabs.loginDevice != "")
            return;   // already showing a code
        loginCode.text = "";
        loginLink.text = "Asking TIDAL for a code…";
        if (!tabs.loginOpen) {
            tabs.loginOpen = true;
            loginSheet.open();
        }
        Tidal.startLogin(function (err, login) {
            if (err) {
                tabs.loginDevice = "";
                loginLink.text = "Could not reach TIDAL (" + err + "). Check the connection and choose New code.";
                return;
            }
            tabs.loginDevice = login.deviceCode;
            tabs.loginExpires = new Date().getTime() + (login.expiresIn ? login.expiresIn : 300) * 1000;
            loginLink.text = login.url.replace(/^https?:\/\//, "");
            loginCode.text = login.code;
            loginTimer.interval = Math.max(2, login.interval) * 1000;
            loginTimer.start();
        });
    }

    function pollLogin() {
        if (tabs.loginDevice == "")
            return;
        if (new Date().getTime() > tabs.loginExpires) {
            loginTimer.stop();
            tabs.loginDevice = "";
            tabs.startLogin();   // the code ran out: get a fresh one
            return;
        }
        Tidal.pollLogin(tabs.loginDevice, function (err, state) {
            if (state == "done") {
                loginTimer.stop();
                tabs.loginDevice = "";
                loginSheet.close();
                downloads.markVerified();
                homeBrowse.load("/api/home", false);
                tabs.toast("Logged in to TIDAL");
            } else if (state == "expired") {
                loginTimer.stop();
                tabs.loginDevice = "";
                tabs.startLogin();   // a fresh code
            } else if (err) {
                console.log("Login check failed: " + err);   // keep trying
            }
        });
    }

    // ---------- song actions, playlists, favourites ----------

    property string pendingTrackIds: ""

    function toast(text) {
        sysToast.body = text;
        sysToast.show();
    }

    // A change at TIDAL (favourite, playlist); shows okText or the error.
    function post(path, okText) {
        Tidal.post(path, function (err) {
            if (err == "needLogin")
                tabs.startLogin();
            else
                tabs.toast(err ? "Failed: " + err : okText);
        });
    }

    function favorite(kind, itemId, okText) {
        tabs.post("/api/favorite/" + kind + "/" + itemId, okText);
    }

    function trackAction(nav, what, d) {
        if (what == "album" && d.albumId != "")
            tabs.openItem(nav, "album", d.albumId, d.album);
        else if (what == "artist" && d.artistId != "")
            tabs.openItem(nav, "artist", d.artistId, d.artist.split(", ")[0]);
        else if (what == "playlist")
            tabs.addToPlaylist([d.id]);
        else if (what == "favorite")
            tabs.favorite("track", d.id, "Added to My tracks");
        else if (what == "download") {
            downloads.add(d);
            tabs.toast("Downloading " + d.title);
        } else if (what == "undownload") {
            downloads.remove(d.id);
            tabs.toast("Download removed");
        }
    }

    function openDownloads(nav) {
        var page = listPageDef.createObject();
        page.main = tabs;
        page.nav = nav;
        page.titleText = "Downloads";
        page.local = true;
        nav.push(page);
        page.showLocal();
    }

    function addToPlaylist(ids) {
        tabs.pendingTrackIds = ids.join(",");
        pickModel.clear();
        newPlaylistName.text = "";
        pickStatus.text = "Loading your playlists…";
        playlistSheet.open();
        Tidal.route("/api/my/own-playlists", function (err, d) {
            if (err || !d || !d.sections) {
                pickStatus.text = "Could not load your playlists" + (err ? ": " + err : ".");
                return;
            }
            var items = d.sections[0].items;
            for (var i = 0; i < items.length; i++)
                pickModel.append({ "id": items[i].id, "title": items[i].title, "sub": items[i].artist });
            pickStatus.text = items.length > 0 ? "" : "You have no playlists yet. Create one above.";
        });
    }

    function addToExisting(p) {
        playlistSheet.close();
        tabs.post("/api/playlist/" + p.id + "/add?tracks=" + tabs.pendingTrackIds, "Added to " + p.title);
    }

    function createPlaylist(name) {
        name = name.replace(/^\s+|\s+$/g, "");
        if (name == "")
            return;
        playlistSheet.close();
        tabs.post("/api/playlists/new?title=" + encodeURIComponent(name) + "&tracks=" + tabs.pendingTrackIds,
                  "Created playlist " + name);
    }

    function fmt(ms) {
        var sec = Math.floor(ms / 1000);
        var s = sec % 60;
        return Math.floor(sec / 60) + ":" + (s < 10 ? "0" : "") + s;
    }

    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem {
            onTriggered: {
                qualityChoice.selectedIndex = app.quality == "LOW" ? 0 : 1;
                clientIdField.text = app.clientId;
                clientSecretField.text = app.clientSecret;
                storageChoice.selectedIndex = downloads.useSdCard ? 1 : 0;
                settingsSheet.open();
            }
        }
        actions: [
            ActionItem {
                title: "Log"
                onTriggered: {
                    logArea.text = app.logText();
                    logSheet.open();
                }
            }
        ]
    }

    onCreationCompleted: {
        downloads.finishedOne.connect(function (title) {
            if (downloads.queued == 0)
                tabs.toast("Downloads finished");
        });
        downloads.failedOne.connect(function (title) {
            tabs.toast("Download failed: " + title);
        });
        downloads.moveFinished.connect(function (moved, failed) {
            tabs.toast(failed > 0 ? "Moved " + moved + " songs, " + failed + " failed (card full?)"
                                  : "Moved " + moved + " songs");
        });
        // The player and the downloads ask for a fresh stream link per song.
        player.needStreamUrl.connect(function (trackId, generation) {
            Tidal.streamUrl(trackId, function (err, url) {
                if (err == "needLogin")
                    tabs.startLogin();
                player.provideStream(generation, err ? "" : url);
            });
        });
        downloads.needStreamUrl.connect(function (trackId) {
            Tidal.streamUrl(trackId, function (err, url) {
                downloads.provideStream(trackId, err ? "" : url);
            });
        });
        app.connectionTested.connect(function (ok, summary) {
            if (!ok)
                tabs.toast(summary + ". See Log for details.");
        });
        app.testConnection();
        tabs.checkLogin();
    }

    attachedObjects: [
        SystemToast {
            id: sysToast
        },
        Sheet {
            id: playlistSheet
            Page {
                titleBar: TitleBar {
                    title: "Add to playlist"
                    dismissAction: ActionItem {
                        title: "Cancel"
                        onTriggered: playlistSheet.close()
                    }
                }
                Container {
                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        leftPadding: 20
                        rightPadding: 20
                        topPadding: 20
                        bottomPadding: 10
                        TextField {
                            id: newPlaylistName
                            hintText: "New playlist name"
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            input {
                                submitKey: SubmitKey.Done
                                onSubmitted: tabs.createPlaylist(newPlaylistName.text)
                            }
                        }
                        Button {
                            text: "Create"
                            preferredWidth: 200
                            verticalAlignment: VerticalAlignment.Center
                            onClicked: tabs.createPlaylist(newPlaylistName.text)
                        }
                    }
                    Label {
                        id: pickStatus
                        visible: text != ""
                        multiline: true
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.color: Color.Gray
                    }
                    ListView {
                        dataModel: ArrayDataModel {
                            id: pickModel
                        }
                        function itemType(data, indexPath) {
                            return "item";
                        }
                        listItemComponents: [
                            ListItemComponent {
                                type: "item"
                                StandardListItem {
                                    title: ListItemData.title
                                    description: ListItemData.sub
                                }
                            }
                        ]
                        onTriggered: tabs.addToExisting(pickModel.data(indexPath))
                    }
                }
            }
        },
        QTimer {
            id: loginTimer
            interval: 3000
            onTimeout: tabs.pollLogin()
        },
        Sheet {
            id: loginSheet
            onClosed: {
                tabs.loginOpen = false;
                tabs.loginDevice = "";
                loginTimer.stop();
            }
            Page {
                titleBar: TitleBar {
                    title: "Log in to TIDAL"
                    dismissAction: ActionItem {
                        title: "Later"
                        onTriggered: loginSheet.close()
                    }
                    acceptAction: ActionItem {
                        title: "New code"
                        onTriggered: {
                            loginTimer.stop();
                            tabs.loginDevice = "";
                            tabs.startLogin();
                        }
                    }
                }
                ScrollView {
                    Container {
                        leftPadding: 36
                        rightPadding: 36
                        topPadding: 30
                        Label {
                            multiline: true
                            text: "Connect the app to your TIDAL account. On any other phone or computer, open this address:"
                        }
                        Label {
                            id: loginLink
                            topMargin: 20
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle.base: SystemDefaults.TextStyles.TitleText
                            textStyle.color: Color.create("#31d0c6")
                        }
                        Label {
                            topMargin: 30
                            multiline: true
                            text: "Log in with your TIDAL account there. If it asks for a code, enter:"
                        }
                        Label {
                            id: loginCode
                            topMargin: 20
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle.base: SystemDefaults.TextStyles.BigText
                            textStyle.fontWeight: FontWeight.Bold
                        }
                        Label {
                            topMargin: 30
                            multiline: true
                            text: "This screen closes by itself once you are logged in. Your login is saved only on this phone, and renews itself; log out in Settings."
                            textStyle.color: Color.Gray
                        }
                    }
                }
            }
        },
        ComponentDefinition {
            id: listPageDef
            source: "ListPage.qml"
        },
        Sheet {
            id: settingsSheet
            Page {
                titleBar: TitleBar {
                    title: "Settings"
                    dismissAction: ActionItem {
                        title: "Cancel"
                        onTriggered: settingsSheet.close()
                    }
                    acceptAction: ActionItem {
                        title: "Save"
                        onTriggered: {
                            app.quality = qualityChoice.selectedIndex == 0 ? "LOW" : "HIGH";
                            downloads.useSdCard = storageChoice.selectedIndex == 1;
                            if (clientIdField.text != app.clientId || clientSecretField.text != app.clientSecret) {
                                app.clientId = clientIdField.text;
                                app.clientSecret = clientSecretField.text;
                            }
                            settingsSheet.close();
                        }
                    }
                }
                ScrollView {
                Container {
                    leftPadding: 24
                    rightPadding: 24
                    topPadding: 24
                    bottomPadding: 40
                    Header {
                        title: "TIDAL account"
                    }
                    Label {
                        multiline: true
                        text: app.loggedIn ? "Logged in. The login is stored only on this phone."
                                           : "Not logged in."
                    }
                    Button {
                        text: app.loggedIn ? "Log out" : "Log in"
                        horizontalAlignment: HorizontalAlignment.Fill
                        onClicked: {
                            settingsSheet.close();
                            if (app.loggedIn)
                                app.logout();
                            tabs.startLogin();
                        }
                    }
                    Header {
                        topMargin: 40
                        title: "Streaming quality"
                    }
                    SegmentedControl {
                        id: qualityChoice
                        Option {
                            text: "Data saver (96k)"
                        }
                        Option {
                            text: "Normal (320k)"
                        }
                    }
                    Label {
                        multiline: true
                        text: "Also used for new downloads. Lossless is not available for this app."
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.Gray
                    }
                    Header {
                        topMargin: 40
                        title: "Save downloads on"
                    }
                    SegmentedControl {
                        id: storageChoice
                        Option {
                            text: "Phone"
                        }
                        Option {
                            text: "SD card"
                        }
                    }
                    Label {
                        multiline: true
                        visible: storageChoice.selectedIndex == 1 && !downloads.sdCardAvailable
                        text: "No SD card found, or the app may not use it. Check that a card is inserted and that Tidal-BB10 has the Shared Files permission (Settings → Security and Privacy → Application Permissions). Until then, downloads are saved on the phone."
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.create("#ff6b6b")
                    }
                    Label {
                        multiline: true
                        text: "Downloads are encrypted, so they only play in this app. Songs you downloaded earlier stay where they are; move them from My music → Downloads."
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.Gray
                    }
                    Header {
                        topMargin: 40
                        title: "Advanced"
                    }
                    Label {
                        multiline: true
                        text: "The app's identifier at TIDAL (not your account). Only change this if logging in stops working; clear both fields to go back to the default."
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.Gray
                    }
                    TextField {
                        id: clientIdField
                        hintText: "Client ID"
                    }
                    TextField {
                        id: clientSecretField
                        hintText: "Client secret"
                        inputMode: TextFieldInputMode.Password
                    }
                }
                }
            }
        },
        Sheet {
            id: logSheet
            Page {
                titleBar: TitleBar {
                    title: "Log"
                    dismissAction: ActionItem {
                        title: "Close"
                        onTriggered: logSheet.close()
                    }
                }
                ScrollView {
                    TextArea {
                        id: logArea
                        editable: false
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                    }
                }
            }
        }
    ]

    Tab {
        id: homeTab
        title: "Home"
        imageSource: "asset:///images/home.png"
        NavigationPane {
            id: homeNav
            onPopTransitionEnded: page.destroy()
            Page {
                titleBar: TitleBar {
                    title: "Home"
                }
                Browse {
                    id: homeBrowse
                    onOpenItem: tabs.openItem(homeNav, kind, itemId, title)
                    onTrackMenu: tabs.trackAction(homeNav, what, track)
                    onNeedLogin: tabs.startLogin()
                    onCreationCompleted: {
                        if (app.loggedIn)
                            homeBrowse.load("/api/home", false);
                    }
                }
                actions: [
                    ActionItem {
                        title: "Refresh"
                        imageSource: "asset:///images/refresh.png"
                        ActionBar.placement: ActionBarPlacement.OnBar
                        onTriggered: homeBrowse.load("/api/home?fresh=1", false)
                    },
                    ActionItem {
                        title: player.playing ? "Pause" : "Play"
                        imageSource: player.playing ? "asset:///images/pause.png" : "asset:///images/play.png"
                        enabled: player.hasTrack
                        ActionBar.placement: ActionBarPlacement.OnBar
                        onTriggered: player.toggle()
                    }
                ]
            }
        }
    }

    Tab {
        title: "Search"
        imageSource: "asset:///images/search.png"
        NavigationPane {
            id: searchNav
            onPopTransitionEnded: page.destroy()
            Page {
                Container {
                    Container {
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 14
                        bottomPadding: 10
                        TextField {
                            id: query
                            hintText: "Search Tidal"
                            input {
                                submitKey: SubmitKey.Search
                                onSubmitted: {
                                    if (query.text.length > 0)
                                        searchBrowse.load("/api/search?q=" + encodeURIComponent(query.text), false);
                                }
                            }
                        }
                    }
                    Browse {
                        id: searchBrowse
                        onOpenItem: tabs.openItem(searchNav, kind, itemId, title)
                        onTrackMenu: tabs.trackAction(searchNav, what, track)
                        onNeedLogin: tabs.startLogin()
                    }
                }
            }
        }
    }

    Tab {
        title: "My music"
        imageSource: "asset:///images/library.png"
        NavigationPane {
            id: libraryNav
            onPopTransitionEnded: page.destroy()
            Page {
                titleBar: TitleBar {
                    title: "My music"
                }
                ListView {
                    dataModel: ArrayDataModel {
                        id: libraryModel
                    }
                    function itemType(data, indexPath) {
                        return "item";
                    }
                    listItemComponents: [
                        ListItemComponent {
                            type: "item"
                            StandardListItem {
                                title: ListItemData.title
                            }
                        }
                    ]
                    onCreationCompleted: {
                        libraryModel.append({ "title": "Downloads", "what": "downloads" });
                        libraryModel.append({ "title": "Mixes", "what": "mixes" });
                        libraryModel.append({ "title": "Playlists", "what": "playlists" });
                        libraryModel.append({ "title": "Tracks", "what": "tracks" });
                        libraryModel.append({ "title": "Albums", "what": "albums" });
                        libraryModel.append({ "title": "Artists", "what": "artists" });
                    }
                    onTriggered: {
                        var d = libraryModel.data(indexPath);
                        if (d.what == "downloads")
                            tabs.openDownloads(libraryNav);
                        else
                            tabs.openItem(libraryNav, "my", d.what, d.title);
                    }
                }
            }
        }
    }

    Tab {
        title: "Playing"
        imageSource: "asset:///images/playing.png"
        NowPlaying {
            id: nowPlaying
            onTrackMenu: {
                if (what == "album" || what == "artist")
                    tabs.activeTab = homeTab;
                tabs.trackAction(homeNav, what, track);
            }
        }
    }
}
