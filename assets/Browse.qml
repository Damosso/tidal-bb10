import bb.cascades 1.2
import tidal 1.0
import "tidal.js" as Tidal

// A list filled from one of the /api/... page addresses answered by tidal.js.
Container {
    id: browse

    property int seq: 0
    property string basePath: ""

    signal openItem(string kind, string itemId, string title)
    signal needLogin()
    // Long-press menu on a song: what is album, artist, playlist, favorite,
    // download or undownload.
    signal trackMenu(string what, variant track)

    property string emptyText: "Nothing found."

    // All songs in the list, for "download all" and "add all to playlist".
    function tracks() {
        var out = [];
        for (var i = 0; i < listModel.size(); i++) {
            var x = listModel.value(i);
            if (x.type == "track")
                out.push(x);
        }
        return out;
    }

    function fmt(sec) {
        var s = Math.floor(sec % 60);
        return Math.floor(sec / 60) + ":" + (s < 10 ? "0" : "") + s;
    }

    function clearList() {
        seq = seq + 1;   // ignore any request still running
        listModel.clear();
        status.text = "";
        busy.running = false;
    }

    function load(path, append) {
        if (!append) {
            listModel.clear();
            basePath = path.split("?")[0];
        }
        status.text = "";
        busy.running = true;
        seq = seq + 1;
        var mySeq = seq;
        Tidal.route(path, function (err, data) {
            if (mySeq != browse.seq)
                return;   // a newer request replaced this one
            busy.running = false;
            if (err == "needLogin") {
                status.text = "Log in to TIDAL to see this.";
                browse.needLogin();
                return;
            }
            if (err || !data || !data.sections) {
                status.text = err ? err : "Something went wrong.";
                return;
            }
            show(data, append);
        });
    }

    function show(data, append) {
        var count = 0;
        for (var s = 0; s < data.sections.length; s++) {
            var sec = data.sections[s];
            if (!sec.items || sec.items.length == 0)
                continue;
            if (sec.title)
                listModel.append({ "type": "header", "title": sec.title });
            for (var i = 0; i < sec.items.length; i++) {
                var it = sec.items[i];
                if (it.type == "track" && !it.ok)
                    continue;
                var row = {
                    "type": it.type,
                    "id": "" + it.id,
                    "title": it.title ? it.title : "",
                    "sec": s,
                    "img": it.imgUrl ? it.imgUrl : "",
                    "sub": "",
                    "dur": ""
                };
                if (it.type == "track") {
                    row.artist = it.artist ? it.artist : "";
                    row.album = it.album ? it.album : "";
                    row.duration = it.duration;
                    row.albumId = it.albumId ? "" + it.albumId : "";
                    row.artistId = it.artistId ? "" + it.artistId : "";
                    row.sub = row.artist + (row.album ? " · " + row.album : "");
                    row.saved = downloads.state(row.id) == "done";
                    row.dur = (row.saved ? "● " : "") + fmt(it.duration);
                } else if (it.type == "artist") {
                    row.sub = "Artist";
                } else {
                    row.sub = (it.artist ? it.artist : "") + (it.year ? " · " + it.year : "");
                }
                listModel.append(row);
                count++;
            }
        }
        if (data.next)
            listModel.append({ "type": "more", "title": "Load more",
                "path": basePath + "?offset=" + data.next });
        if (count == 0 && !append)
            status.text = browse.emptyText;
    }

    ActivityIndicator {
        id: busy
        horizontalAlignment: HorizontalAlignment.Center
        preferredHeight: 80
        visible: running
    }

    Label {
        id: status
        multiline: true
        visible: text != ""
        horizontalAlignment: HorizontalAlignment.Center
        textStyle.textAlign: TextAlign.Center
        textStyle.color: Color.Gray
        topMargin: 40
    }

    ListView {
        id: list

        dataModel: ArrayDataModel {
            id: listModel
        }

        function itemType(data, indexPath) {
            if (data.type == "header" || data.type == "more" || data.type == "track")
                return data.type;
            return "item";
        }

        function trackAction(what, data) {
            browse.trackMenu(what, data);
        }

        listItemComponents: [
            ListItemComponent {
                type: "header"
                Header {
                    title: ListItemData.title
                }
            },
            ListItemComponent {
                type: "more"
                StandardListItem {
                    title: ListItemData.title
                }
            },
            ListItemComponent {
                type: "track"
                CustomListItem {
                    id: trackRow
                    dividerVisible: true
                    contextActions: [
                        ActionSet {
                            title: ListItemData.title
                            subtitle: ListItemData.artist
                            ActionItem {
                                title: "Go to album"
                                enabled: ListItemData.albumId != ""
                                onTriggered: trackRow.ListItem.view.trackAction("album", ListItemData)
                            }
                            ActionItem {
                                title: "Go to artist"
                                enabled: ListItemData.artistId != ""
                                onTriggered: trackRow.ListItem.view.trackAction("artist", ListItemData)
                            }
                            ActionItem {
                                title: "Add to playlist"
                                onTriggered: trackRow.ListItem.view.trackAction("playlist", ListItemData)
                            }
                            ActionItem {
                                title: "Add to My tracks"
                                onTriggered: trackRow.ListItem.view.trackAction("favorite", ListItemData)
                            }
                            ActionItem {
                                title: ListItemData.saved ? "Remove download" : "Download"
                                onTriggered: trackRow.ListItem.view.trackAction(ListItemData.saved ? "undownload" : "download", ListItemData)
                            }
                        }
                    ]
                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        leftPadding: 14
                        rightPadding: 14
                        WebImageView {
                            url: ListItemData.img
                            preferredWidth: 92
                            preferredHeight: 92
                            minWidth: 92
                            verticalAlignment: VerticalAlignment.Center
                            scalingMethod: ScalingMethod.AspectFill
                        }
                        Container {
                            leftPadding: 18
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            Label {
                                text: ListItemData.title
                                bottomMargin: 0
                                textStyle.base: SystemDefaults.TextStyles.PrimaryText
                            }
                            Label {
                                text: ListItemData.sub
                                topMargin: 0
                                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                                textStyle.color: Color.Gray
                            }
                        }
                        Label {
                            text: ListItemData.dur
                            verticalAlignment: VerticalAlignment.Center
                            textStyle.base: SystemDefaults.TextStyles.SmallText
                            textStyle.color: ListItemData.saved ? Color.create("#31d0c6") : Color.Gray
                        }
                    }
                }
            },
            ListItemComponent {
                type: "item"
                CustomListItem {
                    dividerVisible: true
                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        leftPadding: 14
                        rightPadding: 14
                        WebImageView {
                            url: ListItemData.img
                            preferredWidth: 92
                            preferredHeight: 92
                            minWidth: 92
                            verticalAlignment: VerticalAlignment.Center
                            scalingMethod: ScalingMethod.AspectFill
                        }
                        Container {
                            leftPadding: 18
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            Label {
                                text: ListItemData.title
                                bottomMargin: 0
                                textStyle.base: SystemDefaults.TextStyles.PrimaryText
                            }
                            Label {
                                text: ListItemData.sub
                                topMargin: 0
                                textStyle.base: SystemDefaults.TextStyles.SubtitleText
                                textStyle.color: Color.Gray
                            }
                        }
                        Label {
                            text: ListItemData.dur
                            verticalAlignment: VerticalAlignment.Center
                            textStyle.base: SystemDefaults.TextStyles.SmallText
                            textStyle.color: Color.Gray
                        }
                    }
                }
            }
        ]

        onTriggered: {
            var d = listModel.data(indexPath);
            if (d.type == "track") {
                var queue = [];
                var start = 0;
                for (var i = 0; i < listModel.size(); i++) {
                    var x = listModel.value(i);
                    if (x.type == "track" && x.sec == d.sec) {
                        if (i == indexPath[0])
                            start = queue.length;
                        queue.push(x);
                    }
                }
                player.playList(queue, start);
            } else if (d.type == "more") {
                listModel.removeAt(indexPath[0]);
                browse.load(d.path, true);
            } else if (d.type != "header") {
                browse.openItem(d.type, d.id, d.title);
            }
        }
    }
}
