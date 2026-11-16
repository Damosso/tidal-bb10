import bb.cascades 1.2
import tidal 1.0

// The Playing tab: cover, track details, seek bar and transport controls
// over a dimmed copy of the cover.
Page {
    id: nowPage

    property bool dragging: false

    // Song actions, handled in main.qml: album, artist, playlist,
    // favorite, download, undownload.
    signal trackMenu(string what, variant track)

    function fmt(ms) {
        var sec = Math.floor(ms / 1000);
        var s = sec % 60;
        return Math.floor(sec / 60) + ":" + (s < 10 ? "0" : "") + s;
    }

    function largeCover(url) {
        if (url == "")
            return "";
        return url.replace("/160x160.jpg", "/320x320.jpg");
    }

    function sync() {
        if (!nowPage.dragging)
            seekBar.value = player.position;
    }

    onCreationCompleted: {
        player.positionChanged.connect(nowPage.sync);
        player.trackChanged.connect(nowPage.refreshSaved);
    }

    function act(what) {
        if (player.hasTrack)
            nowPage.trackMenu(what, player.track);
    }

    property bool saved: player.hasTrack && downloadState == "done"
    property string downloadState: ""
    property int downloadCount: downloads.count
    property int downloadQueue: downloads.queued
    function refreshSaved() {
        nowPage.downloadState = player.hasTrack ? downloads.state(player.track.id) : "";
    }
    onDownloadCountChanged: refreshSaved()
    onDownloadQueueChanged: refreshSaved()

    actions: [
        ActionItem {
            title: "Go to album"
            enabled: player.hasTrack && player.track.albumId != ""
            onTriggered: nowPage.act("album")
        },
        ActionItem {
            title: "Go to artist"
            enabled: player.hasTrack && player.track.artistId != ""
            onTriggered: nowPage.act("artist")
        },
        ActionItem {
            title: "Add to playlist"
            enabled: player.hasTrack
            onTriggered: nowPage.act("playlist")
        },
        ActionItem {
            title: "Add to My tracks"
            enabled: player.hasTrack
            onTriggered: nowPage.act("favorite")
        },
        ActionItem {
            title: nowPage.saved ? "Remove download" : (nowPage.downloadState != "" ? "Downloading…" : "Download")
            enabled: player.hasTrack && (nowPage.saved || nowPage.downloadState == "")
            onTriggered: nowPage.act(nowPage.saved ? "undownload" : "download")
        }
    ]

    Container {
        layout: DockLayout {
        }
        background: Color.Black

        // Backdrop: the cover, stretched and dimmed.
        WebImageView {
            url: nowPage.largeCover(player.cover)
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFill
            opacity: 0.22
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            leftPadding: 36
            rightPadding: 36

            // Cover and track details
            Container {
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Container {
                    layout: DockLayout {
                    }
                    background: Color.create("#1b2128")
                    preferredWidth: 270
                    preferredHeight: 270
                    minWidth: 270
                    minHeight: 270
                    WebImageView {
                        url: nowPage.largeCover(player.cover)
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        scalingMethod: ScalingMethod.AspectFill
                    }
                }
                Container {
                    leftPadding: 32
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    Label {
                        text: player.hasTrack ? player.title : "Nothing playing"
                        multiline: true
                        autoSize {
                            maxLineCount: 3
                        }
                        bottomMargin: 6
                        textStyle.base: SystemDefaults.TextStyles.TitleText
                        textStyle.fontWeight: FontWeight.Bold
                    }
                    Label {
                        text: player.hasTrack ? player.artist : "Pick a track in Search or My music"
                        multiline: true
                        autoSize {
                            maxLineCount: 2
                        }
                        topMargin: 0
                        bottomMargin: 6
                        textStyle.base: SystemDefaults.TextStyles.BodyText
                        textStyle.color: player.hasTrack ? Color.create("#31d0c6") : Color.Gray
                        gestureHandlers: [
                            TapHandler {
                                onTapped: nowPage.act("artist")
                            }
                        ]
                    }
                    Label {
                        text: player.album + (player.offline ? "  · downloaded" : "")
                        visible: player.album != ""
                        gestureHandlers: [
                            TapHandler {
                                onTapped: nowPage.act("album")
                            }
                        ]
                        topMargin: 0
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.create("#9aa5b1")
                    }
                    Label {
                        text: player.status
                        visible: player.status != ""
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.create("#ff6b6b")
                    }
                }
            }

            // Seek bar with elapsed and total time
            Container {
                topMargin: 34
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Label {
                    text: nowPage.fmt(player.position)
                    minWidth: 80
                    verticalAlignment: VerticalAlignment.Center
                    textStyle.base: SystemDefaults.TextStyles.SmallText
                    textStyle.color: Color.create("#9aa5b1")
                }
                Slider {
                    id: seekBar
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    fromValue: 0
                    toValue: Math.max(1, player.duration)
                    enabled: player.hasTrack && !player.loading
                    onTouch: {
                        if (event.isDown())
                            nowPage.dragging = true;
                        else if (event.isUp() || event.isCancel())
                            nowPage.dragging = false;
                    }
                    onValueChanged: {
                        // Only a drag moves the bar far from the playing position.
                        if (Math.abs(value - player.position) > 2500)
                            player.seek(Math.round(value));
                    }
                }
                Label {
                    text: nowPage.fmt(player.duration)
                    minWidth: 80
                    verticalAlignment: VerticalAlignment.Center
                    textStyle.base: SystemDefaults.TextStyles.SmallText
                    textStyle.color: Color.create("#9aa5b1")
                    textStyle.textAlign: TextAlign.Right
                }
            }

            // Shuffle / previous / play-pause / next
            Container {
                topMargin: 22
                horizontalAlignment: HorizontalAlignment.Center
                opacity: player.hasTrack ? 1.0 : 0.35
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                ImageButton {
                    defaultImageSource: player.shuffle ? "asset:///images/ctl_shuffle_on.png" : "asset:///images/ctl_shuffle_off.png"
                    pressedImageSource: "asset:///images/ctl_shuffle_on.png"
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: 20
                    onClicked: player.shuffle = !player.shuffle
                }
                ImageButton {
                    defaultImageSource: "asset:///images/ctl_prev.png"
                    pressedImageSource: "asset:///images/ctl_prev_down.png"
                    disabledImageSource: "asset:///images/ctl_prev_down.png"
                    enabled: player.hasTrack
                    verticalAlignment: VerticalAlignment.Center
                    rightMargin: 30
                    onClicked: player.previous()
                }
                Container {
                    layout: DockLayout {
                    }
                    preferredWidth: 150
                    preferredHeight: 150
                    ImageButton {
                        defaultImageSource: player.playing ? "asset:///images/ctl_pause.png" : "asset:///images/ctl_play.png"
                        pressedImageSource: player.playing ? "asset:///images/ctl_pause_down.png" : "asset:///images/ctl_play_down.png"
                        disabledImageSource: "asset:///images/ctl_play_down.png"
                        enabled: player.hasTrack
                        visible: !player.loading
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        onClicked: player.toggle()
                    }
                    ActivityIndicator {
                        running: player.loading
                        visible: player.loading
                        preferredWidth: 110
                        preferredHeight: 110
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                    }
                }
                ImageButton {
                    defaultImageSource: "asset:///images/ctl_next.png"
                    pressedImageSource: "asset:///images/ctl_next_down.png"
                    disabledImageSource: "asset:///images/ctl_next_down.png"
                    enabled: player.hasTrack
                    verticalAlignment: VerticalAlignment.Center
                    leftMargin: 30
                    onClicked: player.next()
                }
                // Balances the shuffle button so play stays centred.
                Container {
                    preferredWidth: 84
                    minWidth: 84
                    leftMargin: 20
                }
            }
        }
    }
}
