import bb.cascades 1.2
import tidal 1.0

TabbedPane {
    id: tabs
    showTabsOnActionBar: true

    function openItem(nav, kind, itemId, title) {
        var page = listPageDef.createObject();
        page.main = tabs;
        page.nav = nav;
        page.titleText = title;
        nav.push(page);
        page.load("/api/" + kind + "/" + itemId);
    }

    function fmt(ms) {
        var sec = Math.floor(ms / 1000);
        var s = sec % 60;
        return Math.floor(sec / 60) + ":" + (s < 10 ? "0" : "") + s;
    }

    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem {
            onTriggered: {
                serverField.text = app.server;
                passwordField.text = app.password;
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
        if (app.server == "") {
            settingsSheet.open();
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: listPageDef
            source: "ListPage.qml"
        },
        Sheet {
            id: settingsSheet
            Page {
                titleBar: TitleBar {
                    title: "Server"
                    dismissAction: ActionItem {
                        title: "Cancel"
                        onTriggered: settingsSheet.close()
                    }
                    acceptAction: ActionItem {
                        title: "Save"
                        onTriggered: {
                            app.server = serverField.text;
                            app.password = passwordField.text;
                            homeBrowse.load("/api/home?fresh=1", false);
                            settingsSheet.close();
                        }
                    }
                }
                Container {
                    leftPadding: 24
                    rightPadding: 24
                    topPadding: 24
                    Label {
                        multiline: true
                        text: "Address of the Tidal bridge running on your computer, as printed when it starts."
                    }
                    TextField {
                        id: serverField
                        hintText: "192.168.1.10:8686 or myserver.example:8686"
                        inputMode: TextFieldInputMode.Url
                    }
                    Label {
                        multiline: true
                        topMargin: 30
                        text: "Password, if the bridge was started with one. Leave empty otherwise."
                    }
                    TextField {
                        id: passwordField
                        hintText: "Password"
                        inputMode: TextFieldInputMode.Password
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
                    onCreationCompleted: {
                        if (app.server != "")
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
                        libraryModel.append({ "title": "Mixes", "what": "mixes" });
                        libraryModel.append({ "title": "Playlists", "what": "playlists" });
                        libraryModel.append({ "title": "Tracks", "what": "tracks" });
                        libraryModel.append({ "title": "Albums", "what": "albums" });
                        libraryModel.append({ "title": "Artists", "what": "artists" });
                    }
                    onTriggered: {
                        var d = libraryModel.data(indexPath);
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
        }
    }
}
