import bb.cascades 1.2
import tidal 1.0

// A list filled from one of the bridge server's /api/... addresses.
Container {
    id: browse

    property int seq: 0
    property string basePath: ""

    signal openItem(string kind, string itemId, string title)

    function fmt(sec) {
        var s = Math.floor(sec % 60);
        return Math.floor(sec / 60) + ":" + (s < 10 ? "0" : "") + s;
    }

    function load(path, append) {
        if (app.server == "") {
            status.text = "Set the server address first: swipe down from the top and choose Settings.";
            return;
        }
        if (!append) {
            listModel.clear();
            basePath = path.split("?")[0];
        }
        status.text = "";
        busy.running = true;
        seq = seq + 1;
        var mySeq = seq;
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function () {
            if (xhr.readyState != 4 || mySeq != browse.seq)
                return;
            busy.running = false;
            var data = null;
            try {
                data = JSON.parse(xhr.responseText);
            } catch (e) {
            }
            if (xhr.status == 401) {
                status.text = "The server wants a password. Swipe down from the top and choose Settings.";
                return;
            }
            if (xhr.status != 200 || !data || !data.sections) {
                status.text = (data && data.error) ? "Server: " + data.error
                    : "Cannot reach the server at " + app.server;
                return;
            }
            show(data, append);
        };
        xhr.open("GET", app.url(path));
        xhr.send();
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
                    "img": it.img ? app.url("/cover/" + it.img) : "",
                    "sub": "",
                    "dur": ""
                };
                if (it.type == "track") {
                    row.artist = it.artist ? it.artist : "";
                    row.album = it.album ? it.album : "";
                    row.duration = it.duration;
                    row.stream = app.url("/stream/" + it.id);
                    row.sub = row.artist + (row.album ? " · " + row.album : "");
                    row.dur = fmt(it.duration);
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
            status.text = "Nothing found.";
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
            if (data.type == "header" || data.type == "more")
                return data.type;
            return "item";
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
