import bb.cascades 1.2

// An album, artist, playlist or library list pushed onto a tab.
Page {
    id: page

    property variant main
    property variant nav
    property alias titleText: bar.title

    function load(path) {
        browse.load(path, false);
    }

    titleBar: TitleBar {
        id: bar
    }

    Browse {
        id: browse
        onOpenItem: page.main.openItem(page.nav, kind, itemId, title)
        onNeedLogin: page.main.startLogin()
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
        }
    ]
}
