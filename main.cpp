#include <QApplication>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QStandardPaths>
#include <QDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QSettings>
#include <QCloseEvent>
#include <QDebug>
#include <QActionGroup>
#include <QPainter>
#include <QFont>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QEventLoop>
#include <QTimer>


#ifdef Q_OS_UNIX
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/XF86keysym.h>
#endif

// ---------------- helpers ----------------

QIcon createEmojiIcon(const QString &emoji) {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QFont font("Noto Color Emoji", 36);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);
    painter.end();
    return QIcon(pixmap);
}

QString findFirstMprisService() {
    QDBusInterface dbusIface("org.freedesktop.DBus",
                             "/org/freedesktop/DBus",
                             "org.freedesktop.DBus",
                             QDBusConnection::sessionBus());
    if (!dbusIface.isValid()) {
        qWarning() << "DBus interface invalid:" << QDBusConnection::sessionBus().lastError().message();
        return QString();
    }
    QDBusReply<QStringList> reply = dbusIface.call("ListNames");
    if (!reply.isValid()) {
        qWarning() << "ListNames failed:" << reply.error().message();
        return QString();
    }
    for (const QString &name: reply.value()) {
        if (name.startsWith("org.mpris.MediaPlayer2.")) {
            return name;
        }
    }
    return QString();
}

bool sendMprisCommand(const QString &command) {
    QString service = findFirstMprisService();
    if (service.isEmpty()) return false;
    QDBusInterface playerIface(service,
                               "/org/mpris/MediaPlayer2",
                               "org.mpris.MediaPlayer2.Player",
                               QDBusConnection::sessionBus());
    if (!playerIface.isValid()) return false;
    QDBusReply<void> reply = playerIface.call(command);
    return reply.isValid();
}

#ifdef Q_OS_UNIX
bool sendX11MediaKey(KeySym keysym) {
    Display *display = XOpenDisplay(nullptr);
    if (!display) return false;
    KeyCode keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
        XCloseDisplay(display);
        return false;
    }
    XTestFakeKeyEvent(display, keycode, True, CurrentTime);
    XTestFakeKeyEvent(display, keycode, False, CurrentTime);
    XFlush(display);
    XCloseDisplay(display);
    return true;
}
#endif

bool sendSystemMediaCommand(const QString &command) {
    if (sendMprisCommand(command)) return true;
#ifdef Q_OS_UNIX
    if (command == "PlayPause") return sendX11MediaKey(XF86XK_AudioPlay);
    if (command == "Next") return sendX11MediaKey(XF86XK_AudioNext);
    if (command == "Previous") return sendX11MediaKey(XF86XK_AudioPrev);
#endif
    return false;
}

// 在 QWebEnginePage 上执行 JS，按优先级尝试多个选择器并支持 Shadow DOM，返回是否点击成功
static bool clickPlayerButton(QWebEnginePage *page, const QString &selectors, int timeoutMs = 1200) {
    if (!page) return false;

    static const char *js_template = R"JS(
(function(selectors){
    function findInRoot(root, sel) {
        try {
            var el = root.querySelector(sel);
            if (el) return el;
        } catch(e){}
        var nodes = root.querySelectorAll('*');
        for (var i=0;i<nodes.length;i++){
            var n = nodes[i];
            if (n && n.shadowRoot) {
                try {
                    var r = findInRoot(n.shadowRoot, sel);
                    if (r) return r;
                } catch(e){}
            }
        }
        return null;
    }

    function dispatchClick(el) {
        try {
            el.focus && el.focus();
            var rect = el.getBoundingClientRect();
            var clientX = rect.left + rect.width/2;
            var clientY = rect.top + rect.height/2;
            ['mousedown','mouseup','click'].forEach(function(type){
                var ev = new MouseEvent(type, {
                    view: window,
                    bubbles: true,
                    cancelable: true,
                    clientX: clientX,
                    clientY: clientY,
                    button: 0
                });
                el.dispatchEvent(ev);
            });
            return true;
        } catch(e){
            try { el.click(); return true; } catch(e2){ return false; }
        }
    }

    var list = [];
    if (Array.isArray(selectors)) list = selectors;
    else list = String(selectors).split(',').map(function(s){ return s.trim(); }).filter(Boolean);

    for (var i=0;i<list.length;i++){
        var sel = list[i];
        try {
            var el = document.querySelector(sel);
            if (!el) el = findInRoot(document, sel);
            if (el) {
                if (dispatchClick(el)) return true;
            }
        } catch(e){}
    }

    var fallback = [
        '#btn_pc_minibar_play',
        'button.play-btn',
        'button.playorPauseIconStyle_p5dzjle',
        'button[title=\"播放\"]',
        'button[title=\"暂停\"]',
        'button[title=\"上一首\"]',
        'button[title=\"下一首\"]',
        'button .cmd-icon.cmd-icon-pre',
        'button .cmd-icon.cmd-icon-next'
    ];
    for (var j=0;j<fallback.length;j++){
        try {
            var e2 = document.querySelector(fallback[j]) || findInRoot(document, fallback[j]);
            if (e2 && dispatchClick(e2)) return true;
        } catch(e){}
    }

    return false;
})
)JS";

    // 需要对 selectors 做 JS 字符串转义（避免引号/反斜杠问题）
    QString esc = selectors;
    esc.replace('\\', "\\\\");
    esc.replace('\'', "\\'");
    esc.replace('\n', "\\n");
    // 将转义后的 selectors 作为单引号字符串传入 JS
    QString js = QString::fromUtf8(js_template) + QString::fromUtf8("\n('%1');").arg(esc);

    bool result = false;
    QEventLoop loop;
    page->runJavaScript(js, [&result, &loop](const QVariant &v) {
        result = v.toBool();
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}


// ---------------- MainWindow ----------------

class MainWindow : public QWidget {
    Q_OBJECT

public:
    MainWindow(QWebEngineView *view, QSystemTrayIcon *trayIcon, const QString &stateFilePath, QWidget *parent = nullptr)
        : QWidget(parent), m_view(view), m_trayIcon(trayIcon), m_stateFilePath(stateFilePath) {
        QVBoxLayout *layout = new QVBoxLayout;
        layout->addWidget(view);
        setLayout(layout);
        resize(1200, 800);
        setWindowTitle("网易云音乐 Web 播放器");
        loadSettings();
    }

    void closeEvent(QCloseEvent *event) override {
        if (m_closeToTray) {
            hide();
            event->ignore();
        } else {
            saveSettings();
            event->accept();
            QApplication::quit();
        }
    }

    // 公共接口：读取/设置关闭到托盘行为
    bool closeToTray() const { return m_closeToTray; }

    void setCloseToTray(bool closeToTray) {
        m_closeToTray = closeToTray;
        saveSettings();
    }

    void loadSettings() {
        QSettings settings(QApplication::organizationName(), QApplication::applicationName());
        m_closeToTray = settings.value("closeToTray", true).toBool();
    }

    void saveSettings() {
        QSettings settings(QApplication::organizationName(), QApplication::applicationName());
        settings.setValue("closeToTray", m_closeToTray);
    }

    QString stateFilePath() const { return m_stateFilePath; }

private:
    QWebEngineView *m_view;
    QSystemTrayIcon *m_trayIcon;
    bool m_closeToTray = true;
    QString m_stateFilePath;
};

// ---------------- JS snippets ----------------

// JS to read player state: returns JSON string with id, time, paused
static const char *js_read_state = R"JS(
(function(){
    try {
        var id = location.hash || location.pathname || document.title || 'unknown';
        var audio = document.querySelector('audio');
        var time = 0;
        var paused = true;
        if (audio) {
            time = audio.currentTime || 0;
            paused = audio.paused;
        } else {
            if (window.player && window.player.getCurrentTime) {
                try { time = window.player.getCurrentTime(); } catch(e) {}
            }
            if (window.player && window.player.isPlaying) {
                try { paused = !window.player.isPlaying(); } catch(e) {}
            }
        }
        return JSON.stringify({id: String(id), time: Number(time), paused: Boolean(paused)});
    } catch(e) {
        return JSON.stringify({id:'unknown', time:0, paused:true});
    }
})();
)JS";

// JS to restore state: expects a JSON object {id, time, paused}
static const char *js_restore_state_template = R"JS(
(function(state){
    try {
        var audio = document.querySelector('audio');
        if (audio && state && typeof state.time === 'number') {
            var setOnce = function() {
                try {
                    if (audio.readyState > 0) {
                        audio.currentTime = Math.min(state.time, audio.duration || state.time);
                        if (!state.paused) audio.play().catch(function(){});
                        return true;
                    }
                } catch(e){}
                return false;
            };
            if (!setOnce()) {
                var tries = 0;
                var t = setInterval(function(){
                    tries++;
                    if (setOnce() || tries > 20) clearInterval(t);
                }, 500);
            }
        } else {
            if (window.player && window.player.seek) {
                try { window.player.seek(state.time); if (!state.paused) window.player.play(); } catch(e) {}
            }
        }
    } catch(e){}
})(%1);
)JS";

// ---------------- main ----------------

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("NeteaseWebPlayer");
    app.setApplicationName("NeteaseWebPlayer");

    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --no-sandbox");

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    QString stateFile = dataDir + "/player_state.json";

    QWebEngineProfile *profile = new QWebEngineProfile("NeteaseWebPlayer", &app);
    profile->setPersistentStoragePath(dataDir + "/storage");
    profile->setCachePath(dataDir + "/cache");
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setHttpCacheMaximumSize(200 * 1024 * 1024);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile->setHttpUserAgent(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36");

    // 注意：某些 Qt 版本没有 ServiceWorkersEnabled 枚举，故不调用该属性以保证兼容性
    profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    profile->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    profile->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    profile->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);

    QWebEnginePage *page = new QWebEnginePage(profile, &app);
    QWebEngineView *view = new QWebEngineView;
    view->setPage(page);

    QUrl playerUrl("https://music.163.com/st/webplayer");
    view->load(playerUrl);

    QObject::connect(view, &QWebEngineView::urlChanged, [view, playerUrl](const QUrl &url) {
        if (!url.isValid() || url.host() != "music.163.com") {
            qDebug() << "Redirecting to player page...";
            view->load(playerUrl);
        }
    });

    // tray icon and window
    QSystemTrayIcon *trayIcon = new QSystemTrayIcon(&app);

    // QIcon icon
    QIcon icon(QDir(QCoreApplication::applicationDirPath()).filePath("favicon.ico"));
    trayIcon->setIcon(icon);
    trayIcon->setToolTip("网易云音乐 Web 播放器");

    MainWindow *window = new MainWindow(view, trayIcon, stateFile);
    window->setWindowIcon(icon);

    QMenu *trayMenu = new QMenu();
    QAction *showAction = trayMenu->addAction("打开主窗口");
    trayMenu->addSeparator();
    QAction *playPauseAction = trayMenu->addAction("播放/暂停");
    QAction *prevAction = trayMenu->addAction("上一曲");
    QAction *nextAction = trayMenu->addAction("下一曲");
    trayMenu->addSeparator();

    QMenu *closeBehaviorMenu = new QMenu("关闭行为", trayMenu);
    QActionGroup *behaviorGroup = new QActionGroup(closeBehaviorMenu);
    behaviorGroup->setExclusive(true);
    QAction *closeToTrayAction = closeBehaviorMenu->addAction("隐藏到托盘");
    closeToTrayAction->setCheckable(true);
    closeToTrayAction->setActionGroup(behaviorGroup);
    QAction *exitDirectlyAction = closeBehaviorMenu->addAction("直接退出");
    exitDirectlyAction->setCheckable(true);
    exitDirectlyAction->setActionGroup(behaviorGroup);
    trayMenu->addMenu(closeBehaviorMenu);
    trayMenu->addSeparator();
    QAction *quitAction = trayMenu->addAction("退出");

    if (window->closeToTray()) closeToTrayAction->setChecked(true);
    else exitDirectlyAction->setChecked(true);

    QObject::connect(closeToTrayAction, &QAction::toggled, [window](bool checked) {
        if (checked) window->setCloseToTray(true);
    });
    QObject::connect(exitDirectlyAction, &QAction::toggled, [window](bool checked) {
        if (checked) window->setCloseToTray(false);
    });

    QObject::connect(showAction, &QAction::triggered, [window]() {
        window->show();
        window->raise();
        window->activateWindow();
    });

    QObject::connect(playPauseAction, &QAction::triggered, [page]() {
        QString sel =
                "#btn_pc_minibar_play, button.play-btn, button.playorPauseIconStyle_p5dzjle, button.play-pause-btn, button[title=\"播放\"], button[title=\"暂停\"], span.cmd-icon.cmd-icon-play";
        bool ok = clickPlayerButton(page, sel);
        if (!ok) {
            qWarning() << "PlayPause click failed, falling back to system command";
            if (!sendSystemMediaCommand("PlayPause"))
                qWarning() << "PlayPause both click and system key failed";
        }
    });

    QObject::connect(prevAction, &QAction::triggered, [page]() {
        QString sel =
                "button[title=\"上一首\"], span.cmd-icon.cmd-icon-pre, button[aria-label=\"pre\"], button.cmd-icon-pre, button .cmd-icon.cmd-icon-pre";
        bool ok = clickPlayerButton(page, sel);
        if (!ok) {
            qWarning() << "Previous click failed, falling back to system command";
            if (!sendSystemMediaCommand("Previous"))
                qWarning() << "Previous both click and system key failed";
        }
    });

    QObject::connect(nextAction, &QAction::triggered, [page]() {
        QString sel =
                "button[title=\"下一首\"], span.cmd-icon.cmd-icon-next, button[aria-label=\"next\"], button.cmd-icon-next, button .cmd-icon.cmd-icon-next";
        bool ok = clickPlayerButton(page, sel);
        if (!ok) {
            qWarning() << "Next click failed, falling back to system command";
            if (!sendSystemMediaCommand("Next"))
                qWarning() << "Next both click and system key failed";
        }
    });


    QObject::connect(quitAction, &QAction::triggered, [&app]() { app.quit(); });

    QObject::connect(trayIcon, &QSystemTrayIcon::activated, [window](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (!window->isVisible() || window->isMinimized()) {
                window->showNormal();
                window->raise();
                window->activateWindow();
            } else {
                window->hide();
            }
        }
    });

    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
    window->show();

    // ---------------- state persistence logic ----------------

    QTimer *stateTimer = new QTimer(&app);
    stateTimer->setInterval(4000); // 4s
    QObject::connect(stateTimer, &QTimer::timeout, [page, stateFile]() {
        page->runJavaScript(QString::fromUtf8(js_read_state), [stateFile](const QVariant &result) {
            if (!result.isValid()) return;
            QString jsonStr = result.toString();
            if (jsonStr.isEmpty()) return;
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError) {
                QFile f(stateFile);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(jsonStr.toUtf8());
                    f.close();
                }
                return;
            }
            QJsonObject obj = doc.object();
            obj["saved_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            QJsonDocument out(obj);
            QFile f(stateFile);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(out.toJson(QJsonDocument::Compact));
                f.close();
            }
        });
    });
    stateTimer->start();

    QObject::connect(view, &QWebEngineView::loadFinished, [page, stateFile](bool ok) {
        if (!ok) return;
        QFile f(stateFile);
        if (!f.exists()) return;
        if (!f.open(QIODevice::ReadOnly)) return;
        QByteArray data = f.readAll();
        f.close();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) return;
        QJsonObject obj = doc.object();
        QJsonObject state;
        state["id"] = obj.value("id").toString(obj.value("id").toString());
        state["time"] = obj.value("time").toDouble(0.0);
        state["paused"] = obj.value("paused").toBool(true);
        QJsonDocument sdoc(state);
        QString stateJson = QString::fromUtf8(sdoc.toJson(QJsonDocument::Compact));
        QString js = QString::fromUtf8(js_restore_state_template).arg(stateJson);
        page->runJavaScript(js);
    });

    QObject::connect(&app, &QApplication::aboutToQuit, [trayIcon, window, stateTimer]() {
        stateTimer->stop();
        window->saveSettings();
        if (trayIcon->isVisible()) trayIcon->hide();
    });

    return app.exec();
}

#include "main.moc"
