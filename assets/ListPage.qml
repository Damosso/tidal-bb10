import bb.cascades 1.2

// An album, artist, playlist, mix or library list pushed onto a tab.
// With local set, it shows the songs downloaded for offline listening.
Page {
    id: page

    property variant main
    property variant nav
    property alias titleText: bar.title
    property string kind: ""      // album, artist, playlist, mix, my
    property string itemId: ""
    property bool local: false

    // Reload the Downloads list whenever a download finishes or is removed.
    property int downloadCount: downloads.count
    onDownloadCountChanged: {
        if (page.local)
            page.showLocal();
    }

    function load(path) {
        browse.load(path, false);
    }

    function showLocal() {
        browse.emptyText = "No downloads yet.\nLong-press a song and choose Download, or open an album or playlist and choose Download all.";
        browse.clearList();
        browse.show({ "sections": [ { "title": "", "items": downloads.list() } ] }, false);
    }

    titleBar: TitleBar {
        id: bar
    }

    Container {
        // Download status, on the Downloads page only.
        Container {
            visible: page.local
            leftPadding: 24
            rightPadding: 24
            topPadding: 14
            bottomPadding: 14
            background: Color.create("#14181d")
            Label {
                text: downloads.queued > 0
                      ? "Downloading " + downloads.current + " (" + downloads.progress + "%) · " + downloads.queued + " to go"
                      : downloads.count + " songs · " + downloads.usage
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            Label {
                visible: downloads.moving
                text: "Moving downloads…"
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                textStyle.color: Color.create("#31d0c6")
            }
            Label {
                visible: !downloads.moving && downloads.misplaced > 0
                multiline: true
                text: downloads.misplaced + " songs are not on the " + (downloads.useSdCard ? "SD card" : "phone")
                      + " yet. Choose \"Move downloads\" in the menu to move them."
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                textStyle.color: Color.Gray
            }
            Label {
                visible: downloads.expired
                multiline: true
                text: "Offline songs are paused: open the app once while online to confirm your TIDAL subscription. This is needed once every 30 days."
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                textStyle.color: Color.create("#ff6b6b")
            }
        }

        Browse {
            id: browse
            onOpenItem: page.main.openItem(page.nav, kind, itemId, title)
            onNeedLogin: page.main.startLogin()
            onTrackMenu: page.main.trackAction(page.nav, what, track)
        }
    }

    actions: [
        ActionItem {
            title: player.playing ? "Pause" : "Play"
            imageSource: player.playing ? "asset:///images/pause.png" : "asset:///images/play.png"
            enabled: player.hasTrack
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: player.toggle()
        },
        ActionItem {
            title: "Next"
            imageSource: "asset:///images/next.png"
            enabled: player.hasTrack
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: player.next()
        },
        ActionItem {
            title: "Download all"
            enabled: !page.local
            onTriggered: {
                var t = browse.tracks();
                downloads.addAll(t);
                page.main.toast(t.length > 0 ? "Downloading " + t.length + " songs" : "No songs to download");
            }
        },
        ActionItem {
            title: "Add all to playlist"
            onTriggered: {
                var t = browse.tracks();
                var ids = [];
                for (var i = 0; i < t.length; i++)
                    ids.push(t[i].id);
                if (ids.length > 0)
                    page.main.addToPlaylist(ids);
            }
        },
        ActionItem {
            title: "Add to My music"
            enabled: page.kind == "album" || page.kind == "artist" || page.kind == "playlist"
            onTriggered: page.main.favorite(page.kind, page.itemId, "Added to My music")
        },
        ActionItem {
            title: downloads.useSdCard ? "Move downloads to SD card" : "Move downloads to phone"
            enabled: page.local && !downloads.moving && downloads.misplaced > 0
                     && (!downloads.useSdCard || downloads.sdCardAvailable)
            onTriggered: downloads.moveAll()
        },
        ActionItem {
            title: "Stop downloading"
            enabled: page.local && downloads.queued > 0
            onTriggered: downloads.cancelQueue()
        },
        DeleteActionItem {
            title: "Remove all downloads"
            enabled: page.local && downloads.count > 0
            onTriggered: {
                downloads.removeAll();
                page.main.toast("All downloads removed");
            }
        }
    ]
}
