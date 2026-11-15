#include <QDeclarativeError>
#include <QtDeclarative>

#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/Label>
#include <bb/cascades/Page>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/ScrollView>

#include "app.hpp"
#include "player.hpp"
#include "webimageview.hpp"

using namespace bb::cascades;

Q_DECL_EXPORT int main(int argc, char **argv)
{
    qInstallMsgHandler(App::messageHandler);
    Application application(argc, argv);

    qmlRegisterType<WebImageView>("tidal", 1, 0, "WebImageView");

    App *app = new App(&application);
    Player *player = new Player(&application);

    QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(&application);
    qml->setContextProperty("app", app);
    qml->setContextProperty("player", player);

    AbstractPane *root = qml->hasErrors() ? 0 : qml->createRootObject<AbstractPane>();
    if (!root) {
        // Show what went wrong instead of a blank screen.
        QString text = "The interface failed to load:\n";
        foreach (const QDeclarativeError &e, qml->errors())
            text += "\n" + e.toString();
        text += "\n\n" + app->logText();
        Label *label = new Label();
        label->setMultiline(true);
        label->setText(text);
        ScrollView *scroll = new ScrollView();
        scroll->setContent(label);
        Page *page = new Page();
        page->setContent(scroll);
        root = page;
    }
    application.setScene(root);
    return Application::exec();
}
