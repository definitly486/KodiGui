#include "ui_kodigui.h"
#include "mainwindow.h"
#include <QDebug>
#include <QProcess>
#include <QtNetwork>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include "playerwindow.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QTextCursor>
#include <QDir>
#include <QTimer>
#include <QLineEdit>
#include <functional>
#include <QtConcurrent>
#include <QFuture>
#include <QPointer>
#include <QDateTime>
#include <QMetaObject>
#include <QTextEdit>
#include <QScrollBar>
#include <QCheckBox>
#include <QStyle>
#include <QIcon>
#include <QComboBox>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QLabel>
#include <QFont>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineExtensionManager>
#include <QWebEngineExtensionInfo>
#include <QWebEngineCertificateError>
#include <QWebEngineSettings>
#include <QFileDialog>
#include <QPushButton>
#include <QMenu>
#include <QStandardPaths>
#include <QEvent>
#include <QTabBar>
#include <QList>
#include <QHash>
#include <QSize>
#include <atomic>
#include <cstdlib>
#include <libssh2.h>
#include <libssh2_sftp.h>

// Выполняет внешнюю команду (используется для "ssh pi@... <cmd>").
// Определена ниже в файле; объявлена здесь, т.к. используется раньше.
void runCommand(const QString &command, const QStringList &args = {});
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

namespace {
QPointer<QTextEdit> debugOutput;
std::atomic_bool debugOutputEnabled{true};

void debugMessageHandler(QtMsgType type, const QMessageLogContext &context,
                         const QString &message)
{
    if (!debugOutputEnabled.load(std::memory_order_relaxed)) {
        if (type == QtFatalMsg)
            abort();
        return;
    }
    QString level;
    switch (type) {
    case QtDebugMsg:    level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg:     level = QStringLiteral("INFO"); break;
    case QtWarningMsg:  level = QStringLiteral("WARNING"); break;
    case QtCriticalMsg: level = QStringLiteral("CRITICAL"); break;
    case QtFatalMsg:    level = QStringLiteral("FATAL"); break;
    }

    QString line = QStringLiteral("[%1] [%2]")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), level);
    if (context.category && *context.category)
        line += QStringLiteral(" [%1]").arg(QString::fromLatin1(context.category));
    line += QStringLiteral(" ") + message;

    const QPointer<QTextEdit> target = debugOutput;
    if (target) {
        QMetaObject::invokeMethod(target, [target, line]() {
            if (!target)
                return;
            target->append(line.toHtmlEscaped());
            // вертикальный — в конец (новые сообщения), горизонтальный — всегда слева
            if (auto *v = target->verticalScrollBar())
                v->setValue(v->maximum());
            if (auto *h = target->horizontalScrollBar())
                h->setValue(h->minimum());
        }, Qt::QueuedConnection);
    }

    if (type == QtFatalMsg)
        abort();
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pythonProcess(new QProcess(this))        // ← обязательно this!
    , kodidlpProcess(new QProcess(this))
    , manager(new QNetworkAccessManager(this))
{
    ui->setupUi(this);

    // Persistent Okko profile: extension manager cannot work with an
    // off-the-record profile. Keep cookies, local storage and extensions
    // between application launches.
    const QString okkoDataPath = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/okko-profile");
    QDir().mkpath(okkoDataPath);
    okkoProfile = new QWebEngineProfile(QStringLiteral("KodiGuiOkko"), this);
    okkoProfile->setPersistentStoragePath(okkoDataPath + QStringLiteral("/storage"));
    okkoProfile->setCachePath(okkoDataPath + QStringLiteral("/cache"));
    okkoProfile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Встроенная вкладка Okko работает внутри Qt WebEngine.
    auto *okkoContainer = new QWidget(ui->tabWidget);
    auto *okkoLayout = new QVBoxLayout(okkoContainer);
    okkoLayout->setContentsMargins(0, 0, 0, 0);
    okkoLayout->setSpacing(4);

    auto *okkoToolbar = new QWidget(okkoContainer);
    auto *okkoToolbarLayout = new QHBoxLayout(okkoToolbar);
    okkoToolbarLayout->setContentsMargins(6, 4, 6, 2);
    auto *installOkkoExtensionButton = new QPushButton(
        QStringLiteral("Установить расширение"), okkoToolbar);
    auto *manageOkkoExtensionsButton = new QPushButton(
        QStringLiteral("Расширения"), okkoToolbar);
    auto *okkoExtensionStatus = new QLabel(
        QStringLiteral("Поддерживаются совместимые расширения Manifest V3"),
        okkoToolbar);
    okkoToolbarLayout->addWidget(installOkkoExtensionButton);
    okkoToolbarLayout->addWidget(manageOkkoExtensionsButton);
    okkoToolbarLayout->addWidget(okkoExtensionStatus, 1);
    okkoLayout->addWidget(okkoToolbar);

    auto *okkoView = new QWebEngineView(okkoContainer);
    okkoView->setObjectName(QStringLiteral("okkoWebView"));
    okkoView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    okkoView->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    okkoView->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    okkoView->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    okkoView->setPage(new QWebEnginePage(okkoProfile, okkoView));
    okkoLayout->addWidget(okkoView, 1);
    ui->tabWidget->addTab(okkoContainer, QStringLiteral("okko.tv"));

    okkoExtensionManager = okkoProfile->extensionManager();
    connect(okkoExtensionManager, &QWebEngineExtensionManager::loadFinished, this,
            [this, okkoExtensionStatus](const QWebEngineExtensionInfo &extension) {
        if (extension.isLoaded()) {
            QSettings settings;
            const QStringList disabledIds =
                settings.value(QStringLiteral("okko/disabledExtensions")).toStringList();
            const bool shouldEnable = !disabledIds.contains(extension.id());
            okkoExtensionManager->setExtensionEnabled(extension, shouldEnable);
            qInfo().noquote() << "[Okko extensions] Loaded:"
                              << extension.name() << extension.path()
                              << (shouldEnable ? "enabled" : "kept disabled");
            okkoExtensionStatus->setText(
                QStringLiteral("%1: %2")
                    .arg(shouldEnable ? QStringLiteral("Расширение активно")
                                      : QStringLiteral("Расширение отключено"),
                         extension.name()));
        } else {
            qWarning().noquote() << "[Okko extensions] Load failed:"
                                 << extension.name() << extension.error();
            okkoExtensionStatus->setText(
                QStringLiteral("Ошибка загрузки: %1").arg(extension.error()));
        }
    });
    connect(okkoExtensionManager, &QWebEngineExtensionManager::installFinished, this,
            [this, okkoExtensionStatus](const QWebEngineExtensionInfo &extension) {
        if (extension.isInstalled()) {
            if (extension.isLoaded()) {
                QSettings settings;
                QStringList disabledIds =
                    settings.value(QStringLiteral("okko/disabledExtensions")).toStringList();
                disabledIds.removeAll(extension.id());
                settings.setValue(QStringLiteral("okko/disabledExtensions"), disabledIds);
                okkoExtensionManager->setExtensionEnabled(extension, true);
            }
            qInfo().noquote() << "[Okko extensions] Installed:"
                              << extension.name() << extension.path();
            okkoExtensionStatus->setText(
                QStringLiteral("Установлено: %1").arg(extension.name()));
            QMessageBox::information(this, QStringLiteral("Расширения Okko"),
                QStringLiteral("Расширение «%1» установлено.").arg(extension.name()));
        } else {
            qWarning().noquote() << "[Okko extensions] Install failed:"
                                 << extension.error();
            okkoExtensionStatus->setText(
                QStringLiteral("Ошибка установки: %1").arg(extension.error()));
            QMessageBox::warning(this, QStringLiteral("Расширения Okko"),
                QStringLiteral("Не удалось установить расширение:\n%1")
                    .arg(extension.error()));
        }
    });

    connect(installOkkoExtensionButton, &QPushButton::clicked, this,
            [this, okkoExtensionStatus]() {
        const QString extensionPath = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Выберите папку расширения с manifest.json"),
            QDir::homePath());
        if (extensionPath.isEmpty())
            return;

        QFile manifestFile(QDir(extensionPath).filePath(QStringLiteral("manifest.json")));
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("Расширения Okko"),
                QStringLiteral("В выбранной папке нет доступного manifest.json."));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument manifestDocument =
            QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !manifestDocument.isObject()) {
            QMessageBox::warning(this, QStringLiteral("Расширения Okko"),
                QStringLiteral("Некорректный manifest.json:\n%1")
                    .arg(parseError.errorString()));
            return;
        }

        const QJsonObject manifest = manifestDocument.object();
        if (manifest.value(QStringLiteral("manifest_version")).toInt() != 3) {
            QMessageBox::warning(this, QStringLiteral("Расширения Okko"),
                QStringLiteral("Qt WebEngine поддерживает только Manifest V3. "
                               "Расширения Manifest V2 установить нельзя."));
            return;
        }

        const QString extensionName =
            manifest.value(QStringLiteral("name")).toString(QStringLiteral("Без имени"));
        okkoExtensionStatus->setText(
            QStringLiteral("Установка: %1…").arg(extensionName));
        qInfo().noquote() << "[Okko extensions] Installing from:" << extensionPath;
        okkoExtensionManager->installExtension(extensionPath);
    });

    connect(manageOkkoExtensionsButton, &QPushButton::clicked, this,
            [this, manageOkkoExtensionsButton]() {
        QMenu menu(manageOkkoExtensionsButton);
        const auto extensions = okkoExtensionManager->extensions();
        if (extensions.isEmpty()) {
            QAction *emptyAction = menu.addAction(
                QStringLiteral("Расширения ещё не установлены"));
            emptyAction->setEnabled(false);
        } else {
            for (const QWebEngineExtensionInfo &extension : extensions) {
                if (!extension.isLoaded())
                    continue;
                QAction *action = menu.addAction(
                    extension.name().isEmpty()
                        ? QStringLiteral("Расширение без имени") : extension.name());
                action->setCheckable(true);
                action->setChecked(extension.isEnabled());
                connect(action, &QAction::toggled, this,
                        [this, extension](bool enabled) {
                    okkoExtensionManager->setExtensionEnabled(extension, enabled);
                    QSettings settings;
                    QStringList disabledIds =
                        settings.value(QStringLiteral("okko/disabledExtensions")).toStringList();
                    if (enabled)
                        disabledIds.removeAll(extension.id());
                    else if (!disabledIds.contains(extension.id()))
                        disabledIds.append(extension.id());
                    settings.setValue(QStringLiteral("okko/disabledExtensions"), disabledIds);
                    qInfo() << "[Okko extensions]" << extension.name()
                            << (enabled ? "enabled" : "disabled");
                });
            }
            if (menu.isEmpty()) {
                QAction *emptyAction = menu.addAction(
                    QStringLiteral("Нет загруженных расширений"));
                emptyAction->setEnabled(false);
            }
        }
        menu.exec(manageOkkoExtensionsButton->mapToGlobal(
            QPoint(0, manageOkkoExtensionsButton->height())));
    });

    // Keep tab captions intact. Give tabs enough minimum width for readable
    // labels; when the complete row does not fit, Qt scrolls the tab bar.
    ui->tabWidget->setElideMode(Qt::ElideNone);
    ui->tabWidget->setUsesScrollButtons(true);
    if (QTabBar *tabBar = ui->tabWidget->tabBar()) {
        tabBar->setElideMode(Qt::ElideNone);
        tabBar->setExpanding(false);
        tabBar->setUsesScrollButtons(true);
        tabBar->setMinimumHeight(tabBar->sizeHint().height());
    }

    // Диагностика загрузки Okko. Ошибки сертификатов только журналируем
    // и оставляем отклонёнными: проверку TLS намеренно не отключаем.
    connect(okkoView, &QWebEngineView::loadStarted, this, []() {
        qInfo() << "[Okko] page load started";
    });
    connect(okkoView, &QWebEngineView::loadProgress, this, [](int progress) {
        qInfo() << "[Okko] load progress:" << progress << "%";
    });
    connect(okkoView, &QWebEngineView::loadFinished, this, [okkoView](bool ok) {
        qInfo() << "[Okko] load finished:" << ok
                << "URL:" << okkoView->url().toString()
                << "title:" << okkoView->title();
    });
    connect(okkoView->page(), &QWebEnginePage::certificateError, this,
            [](const QWebEngineCertificateError &error) {
        qWarning().noquote() << "[Okko] TLS certificate error:"
                            << error.description()
                            << "| URL:" << error.url().toString()
                            << "| type:" << static_cast<int>(error.type())
                            << "| overridable:" << error.isOverridable();
        // Не вызываем acceptCertificate(): недоверенный сертификат блокируется.
    });
    connect(okkoView->page(), &QWebEnginePage::renderProcessTerminated, this,
            [](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
        qWarning() << "[Okko] renderer terminated; status:"
                   << static_cast<int>(status) << "exit code:" << exitCode;
    });
    okkoView->setUrl(QUrl(QStringLiteral("https://okko.tv")));

    // Сенсорная панель управляет навигацией Kodi через JSON-RPC.
    ui->mousePadWidget->setMouseTracking(true);
    ui->mousePadHintLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    ui->mousePadWidget->installEventFilter(this);
    connect(ui->mouseBackButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("back")); });
    connect(ui->mouseContextButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("contextmenu")); });
    connect(ui->mouseHomeButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("home")); });
    connect(ui->mouseUpButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("up")); });
    connect(ui->mouseDownButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("down")); });
    connect(ui->mouseLeftButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("left")); });
    connect(ui->mouseRightButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("right")); });
    connect(ui->mouseSelectButton, &QPushButton::clicked, this,
            [this]() { postInputAction(QStringLiteral("select")); });

    // Кнопки с тематическими иконками: в классической теме они отключаются.
    QList<QPushButton *> themedButtons;
    auto applyTheme = [this](const QString &theme) {
        const bool light = (theme == QStringLiteral("light"));
        const bool rust = (theme == QStringLiteral("rust"));
        const bool classic = (theme == QStringLiteral("classic"));
        if (classic) {
            setStyleSheet(QString());
        } else if (light) {
            setStyleSheet(QStringLiteral(R"QSS(
                QWidget {
                    background-color: #f3f6fa;
                    color: #263445;
                    font-family: "Noto Sans", "DejaVu Sans", sans-serif;
                    font-size: 10pt;
                }
                QMainWindow, QWidget#centralwidget { background-color: #f3f6fa; }
                QTabWidget::pane {
                    border: 1px solid #cbd5e1; border-radius: 9px;
                    top: -1px; background: #ffffff;
                }
                QTabBar::tab {
                    background: #e7edf4; color: #526174;
                    border: 1px solid #cbd5e1; border-bottom: none;
                    padding: 9px 15px; margin: 3px 3px 0 0;
                    border-top-left-radius: 7px; border-top-right-radius: 7px;
                    min-width: 66px;
                }
                QTabBar::tab {
                    padding: 4px 5px; margin: 1px 1px 0 0;
                    min-width: 100px; font-size: 8pt;
                }
                QTabBar::tab:selected {
                    background: #ffffff; color: #087f8c;
                    border-color: #7bbdc2; font-weight: 600;
                }
                QTabBar::tab:hover:!selected { background: #dce7f0; color: #172b40; }
                QPushButton {
                    background: #e3edf5; color: #263c50;
                    border: 1px solid #bdcddd; border-radius: 7px;
                    padding: 7px 13px; min-height: 20px;
                }
                QPushButton:hover { background: #d4e8ee; border-color: #55aeb4; }
                QPushButton:pressed { background: #b9e0df; }
                QPushButton:disabled { background: #edf0f4; color: #9aa6b2; border-color: #d8dee6; }
                QLineEdit, QTextEdit, QTextBrowser, QPlainTextEdit, QComboBox {
                    background: #ffffff; color: #263445;
                    border: 1px solid #c4d0dc; border-radius: 7px;
                    padding: 7px; selection-background-color: #8ed3d1;
                    selection-color: #17313b;
                }
                QLineEdit:focus, QTextEdit:focus, QTextBrowser:focus, QComboBox:focus {
                    border: 1px solid #269da3;
                }
                QComboBox QAbstractItemView {
                    background: #ffffff; color: #263445;
                    selection-background-color: #d4eeee;
                }
                QLabel { color: #405268; }
                QSlider::groove:horizontal { height: 6px; background: #d1dce6; border-radius: 3px; }
                QSlider::sub-page:horizontal { background: #269da3; border-radius: 3px; }
                QSlider::handle:horizontal {
                    background: #42b9b4; width: 16px; margin: -5px 0;
                    border-radius: 8px; border: 2px solid #167f85;
                }
                QCheckBox { spacing: 8px; }
                QCheckBox::indicator {
                    width: 17px; height: 17px; border-radius: 4px;
                    border: 1px solid #9aabba; background: #ffffff;
                }
                QCheckBox::indicator:checked { background: #269da3; border-color: #167f85; }
                QScrollBar:vertical { background: #edf1f5; width: 11px; margin: 2px; border-radius: 5px; }
                QScrollBar::handle:vertical { background: #b5c3d0; min-height: 25px; border-radius: 5px; }
                QScrollBar::handle:vertical:hover { background: #69b9b8; }
                QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
                QStatusBar { background: #e8eef4; color: #617286; border-top: 1px solid #cbd5e1; }
                QMenuBar { background: #e8eef4; color: #263445; }
                QMenuBar::item:selected, QMenu::item:selected { background: #d4eeee; }
                QMenu { background: #ffffff; color: #263445; border: 1px solid #cbd5e1; }
            )QSS"));
        } else if (rust) {
            setStyleSheet(QStringLiteral(R"QSS(
                QWidget {
                    background: transparent;
                    color: #f0dfbd;
                    font-family: "Noto Sans", "DejaVu Sans", sans-serif;
                    font-size: 10pt;
                }
                QMainWindow, QWidget#centralwidget {
                    background-color: #17130e;
                    background-image: url(:/tractor_theme_bg.jpg);
                    background-position: center;
                    background-repeat: no-repeat;
                }
                QTabWidget::pane {
                    border: 1px solid rgba(104, 72, 38, 220);
                    border-radius: 8px; top: -1px;
                    background: rgba(25, 21, 16, 218);
                }
                QTabBar::tab {
                    background: rgba(49, 39, 25, 232);
                    color: #d8bd8b;
                    border: 1px solid #75542f;
                    border-bottom: none;
                    padding: 4px 5px; margin: 1px 1px 0 0;
                    border-top-left-radius: 6px;
                    border-top-right-radius: 6px;
                    min-width: 100px; font-size: 8pt;
                }
                QTabBar::tab:selected {
                    background: rgba(133, 87, 37, 238);
                    color: #fff0cf; border-color: #d59a45;
                    font-weight: 600;
                }
                QTabBar::tab:hover:!selected {
                    background: rgba(91, 64, 34, 240); color: #fff4dc;
                }
                QPushButton {
                    background: rgba(119, 78, 34, 224);
                    color: #fff0d1; border: 1px solid #bd8741;
                    border-radius: 6px; padding: 7px 13px; min-height: 20px;
                }
                QPushButton:hover {
                    background: rgba(157, 105, 43, 238); border-color: #e0ad5d;
                }
                QPushButton:pressed { background: rgba(75, 51, 27, 245); }
                QPushButton:disabled {
                    background: rgba(55, 47, 37, 220); color: #9a8b72;
                    border-color: #5b4a35;
                }
                QLineEdit, QTextEdit, QTextBrowser, QPlainTextEdit, QComboBox {
                    background: rgba(18, 16, 13, 232); color: #f0dfbd;
                    border: 1px solid #76552f; border-radius: 6px;
                    padding: 7px; selection-background-color: #a96d32;
                    selection-color: #fff6e5;
                }
                QLineEdit:focus, QTextEdit:focus, QTextBrowser:focus, QComboBox:focus {
                    border: 1px solid #d5a04e;
                }
                QComboBox QAbstractItemView {
                    background: #282016; color: #f0dfbd;
                    selection-background-color: #76502b;
                }
                QLabel { color: #f0dfbd; background: transparent; }
                QSlider::groove:horizontal {
                    height: 6px; background: #55412b; border-radius: 3px;
                }
                QSlider::sub-page:horizontal { background: #c28a3f; border-radius: 3px; }
                QSlider::handle:horizontal {
                    background: #e0ad5d; width: 16px; margin: -5px 0;
                    border-radius: 8px; border: 2px solid #815727;
                }
                QCheckBox { spacing: 8px; background: transparent; }
                QCheckBox::indicator {
                    width: 17px; height: 17px; border-radius: 4px;
                    border: 1px solid #987044; background: rgba(20, 17, 13, 230);
                }
                QCheckBox::indicator:checked {
                    background: #a96d32; border-color: #e0ad5d;
                }
                QScrollBar:vertical {
                    background: rgba(27, 22, 16, 220); width: 11px;
                    margin: 2px; border-radius: 5px;
                }
                QScrollBar::handle:vertical {
                    background: #80603a; min-height: 25px; border-radius: 5px;
                }
                QScrollBar::handle:vertical:hover { background: #bd8741; }
                QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
                QStatusBar {
                    background: rgba(20, 17, 13, 238); color: #c4a77a;
                    border-top: 1px solid #6d4e2d;
                }
                QMenuBar { background: rgba(20, 17, 13, 238); color: #f0dfbd; }
                QMenuBar::item:selected, QMenu::item:selected { background: #76502b; }
                QMenu {
                    background: #282016; color: #f0dfbd; border: 1px solid #76552f;
                }
            )QSS"));
        } else {
            setStyleSheet(QStringLiteral(R"QSS(
                QWidget {
                    background-color: #171c26; color: #e6edf7;
                    font-family: "Noto Sans", "DejaVu Sans", sans-serif; font-size: 10pt;
                }
                QMainWindow, QWidget#centralwidget { background-color: #171c26; }
                QTabWidget::pane { border: 1px solid #303b4d; border-radius: 9px; top: -1px; background: #1d2532; }
                QTabBar::tab {
                    background: #222c3a; color: #aebbd0; border: 1px solid #303b4d;
                    border-bottom: none; padding: 9px 15px; margin: 3px 3px 0 0;
                    border-top-left-radius: 7px; border-top-right-radius: 7px; min-width: 66px;
                }
                QTabBar::tab {
                    padding: 4px 5px; margin: 1px 1px 0 0;
                    min-width: 100px; font-size: 8pt;
                }
                QTabBar::tab:selected { background: #1d2532; color: #55d6c2; border-color: #3b9e98; font-weight: 600; }
                QTabBar::tab:hover:!selected { background: #2b394b; color: #ffffff; }
                QPushButton {
                    background: #29384b; color: #edf5ff; border: 1px solid #3b5069;
                    border-radius: 7px; padding: 7px 13px; min-height: 20px;
                }
                QPushButton:hover { background: #354b63; border-color: #55b8b0; }
                QPushButton:pressed { background: #1d756f; }
                QPushButton:disabled { background: #252d39; color: #68778b; border-color: #303947; }
                QLineEdit, QTextEdit, QTextBrowser, QPlainTextEdit, QComboBox {
                    background: #101722; color: #dce8f5; border: 1px solid #35445a;
                    border-radius: 7px; padding: 7px; selection-background-color: #237f7a; selection-color: #ffffff;
                }
                QLineEdit:focus, QTextEdit:focus, QTextBrowser:focus, QComboBox:focus { border: 1px solid #55c9bc; }
                QComboBox QAbstractItemView { background: #202a38; color: #e6edf7; selection-background-color: #285c60; }
                QLabel { color: #bdc9d9; }
                QSlider::groove:horizontal { height: 6px; background: #35445a; border-radius: 3px; }
                QSlider::sub-page:horizontal { background: #35b8aa; border-radius: 3px; }
                QSlider::handle:horizontal { background: #69e0d0; width: 16px; margin: -5px 0; border-radius: 8px; border: 2px solid #1d756f; }
                QCheckBox { spacing: 8px; }
                QCheckBox::indicator { width: 17px; height: 17px; border-radius: 4px; border: 1px solid #52647b; background: #101722; }
                QCheckBox::indicator:checked { background: #269b8f; border-color: #55d6c2; }
                QScrollBar:vertical { background: #171f2b; width: 11px; margin: 2px; border-radius: 5px; }
                QScrollBar::handle:vertical { background: #43546b; min-height: 25px; border-radius: 5px; }
                QScrollBar::handle:vertical:hover { background: #55a99f; }
                QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
                QStatusBar { background: #121923; color: #8293aa; border-top: 1px solid #303b4d; }
                QMenuBar { background: #121923; color: #dce8f5; }
                QMenuBar::item:selected, QMenu::item:selected { background: #285c60; }
                QMenu { background: #202a38; color: #e6edf7; border: 1px solid #3b5069; }
            )QSS"));
        }
    };

    // Используем стандартные иконки Qt — дополнительных файлов и зависимостей нет.
    QHash<QPushButton *, QStyle::StandardPixmap> buttonIconMap;
    auto setButtonIcon = [this, &themedButtons, &buttonIconMap](QPushButton *button, QStyle::StandardPixmap icon) {
        if (button) {
            button->setIcon(style()->standardIcon(icon));
            themedButtons.append(button);
            buttonIconMap.insert(button, icon);
        }
    };
    setButtonIcon(ui->startKodidlpButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playIptvUrlButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playPvrChannelButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playDrmStreamButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playDrmHlsButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playYoutubeFileButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playMatchTvFileButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->playMatchTvIptvButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->startYoutubeDownloadButton, QStyle::SP_ArrowDown);
    setButtonIcon(ui->startYoutubeStreamlinkButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->startMatchTvStreamlinkButton, QStyle::SP_ArrowDown);
    setButtonIcon(ui->startDrmDownloadButton, QStyle::SP_ArrowDown);
    setButtonIcon(ui->captureMatchTvUrlButton, QStyle::SP_FileDialogDetailedView);
    setButtonIcon(ui->getonairnow, QStyle::SP_BrowserReload);
    setButtonIcon(ui->refreshSystemMonitorButton, QStyle::SP_BrowserReload);
    setButtonIcon(ui->stopPvrChannelButton, QStyle::SP_MediaStop);
    setButtonIcon(ui->stopDrmPlaybackButton, QStyle::SP_MediaStop);
    setButtonIcon(ui->stopYoutubePlaybackButton, QStyle::SP_MediaStop);
    setButtonIcon(ui->stopYtDlpButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->stopFfmpegButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->stopStreamlinkButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->killMatchTvStreamlinkButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->closeMatchTvPopupButton, QStyle::SP_DialogCloseButton);
    setButtonIcon(ui->killYoutubeStreamlinkButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->closeYoutubePopupButton, QStyle::SP_DialogCloseButton);
    setButtonIcon(ui->stopDrmDownloadButton, QStyle::SP_BrowserStop);
    setButtonIcon(ui->clearUrlFieldsButton, QStyle::SP_DialogResetButton);
    setButtonIcon(ui->clearDrmFieldsButton, QStyle::SP_DialogResetButton);
    setButtonIcon(ui->debugClearButton, QStyle::SP_DialogResetButton);
    setButtonIcon(ui->startKodiButton, QStyle::SP_MediaPlay);
    setButtonIcon(ui->stopKodiButton, QStyle::SP_MediaStop);
    setButtonIcon(ui->rebootKodiButton, QStyle::SP_BrowserReload);
    setButtonIcon(ui->toggleIptvSimpleClientButton, QStyle::SP_DialogApplyButton);
    setButtonIcon(ui->closePopupButton, QStyle::SP_DialogCloseButton);
    setButtonIcon(ui->sendBackActionButton, QStyle::SP_DialogCloseButton);
    setButtonIcon(ui->showStreamlinkInfoButton, QStyle::SP_MessageBoxInformation);

    ui->tabWidget->setTabIcon(0, style()->standardIcon(QStyle::SP_MediaVolume));
    ui->tabWidget->setTabIcon(1, style()->standardIcon(QStyle::SP_DirLinkIcon));
    ui->tabWidget->setTabIcon(2, style()->standardIcon(QStyle::SP_MediaPlay));
    ui->tabWidget->setTabIcon(3, style()->standardIcon(QStyle::SP_ComputerIcon));
    ui->tabWidget->setTabIcon(4, style()->standardIcon(QStyle::SP_ArrowDown));
    ui->tabWidget->setTabIcon(5, style()->standardIcon(QStyle::SP_DialogYesButton));
    ui->tabWidget->setTabIcon(6, style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->tabWidget->setTabIcon(7, style()->standardIcon(QStyle::SP_ComputerIcon));
    ui->tabWidget->setTabIcon(8, style()->standardIcon(QStyle::SP_MessageBoxInformation));
    ui->tabWidget->setTabIcon(9, style()->standardIcon(QStyle::SP_MessageBoxWarning));

    // Компактная панель: все разделы помещаются в одну строку без стрелок прокрутки.
    QTabBar *tabs = ui->tabWidget->tabBar();
    tabs->setUsesScrollButtons(false);
    tabs->setExpanding(true);
    tabs->setElideMode(Qt::ElideRight);
    tabs->setIconSize(QSize(14, 14));

    // Отдельная вкладка настроек внешнего вида.
    auto *settingsPage = new QWidget(ui->tabWidget);
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(24, 24, 24, 24);
    settingsLayout->setSpacing(12);

    auto *appearanceTitle = new QLabel(QStringLiteral("Внешний вид"), settingsPage);
    QFont appearanceFont = appearanceTitle->font();
    appearanceFont.setPointSize(14);
    appearanceFont.setBold(true);
    appearanceTitle->setFont(appearanceFont);
    settingsLayout->addWidget(appearanceTitle);

    auto *appearanceDescription = new QLabel(
        QStringLiteral("Выберите цветовую тему интерфейса KodiGui. Выбор будет сохранён и применён при следующем запуске."),
        settingsPage);
    appearanceDescription->setWordWrap(true);
    settingsLayout->addWidget(appearanceDescription);

    auto *themeCombo = new QComboBox(settingsPage);
    themeCombo->setObjectName(QStringLiteral("themeComboBox"));
    themeCombo->addItem(QStringLiteral("Классическая (как в tab_match_vers2)"),
                         QStringLiteral("classic"));
    themeCombo->addItem(QStringLiteral("Тёмная"), QStringLiteral("dark"));
    themeCombo->addItem(QStringLiteral("Светлая"), QStringLiteral("light"));
    themeCombo->addItem(QStringLiteral("Ржавый трактор"), QStringLiteral("rust"));
    settingsLayout->addWidget(themeCombo);
    settingsLayout->addStretch(1);
    ui->tabWidget->addTab(settingsPage, style()->standardIcon(QStyle::SP_DesktopIcon),
                          QStringLiteral("Настройки"));

    auto *themeSettings = new QSettings(QStringLiteral("definitly486"),
                                        QStringLiteral("KodiGui"), this);
    const QString savedTheme = themeSettings->value(QStringLiteral("appearance/theme"),
                                                     QStringLiteral("classic")).toString();
    const QString initialTheme = (savedTheme == QStringLiteral("light")
                                  || savedTheme == QStringLiteral("dark")
                                  || savedTheme == QStringLiteral("rust"))
                                     ? savedTheme : QStringLiteral("classic");
    themeCombo->setCurrentIndex(themeCombo->findData(initialTheme));
    applyTheme(initialTheme);
    if (initialTheme == QStringLiteral("classic")) {
        for (QPushButton *button : themedButtons)
            button->setIcon(QIcon());
        for (int i = 0; i < ui->tabWidget->count(); ++i)
            ui->tabWidget->setTabIcon(i, QIcon());
    }    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, applyTheme, themeSettings, themeCombo, themedButtons, buttonIconMap](int index) {
        const QString theme = themeCombo->itemData(index).toString();
        applyTheme(theme);
        const bool classic = (theme == QStringLiteral("classic"));
        for (QPushButton *button : themedButtons) {
            const auto it = buttonIconMap.constFind(button);
            if (it != buttonIconMap.cend())
                button->setIcon(classic ? QIcon() : style()->standardIcon(it.value()));
        }
        // В классическом режиме возвращаем стандартные вкладки без пиктограмм.
        const QList<QStyle::StandardPixmap> tabIcons = {
            QStyle::SP_MediaVolume, QStyle::SP_DirLinkIcon,
            QStyle::SP_MediaPlay, QStyle::SP_ComputerIcon,
            QStyle::SP_ArrowDown, QStyle::SP_DialogYesButton,
            QStyle::SP_FileDialogContentsView, QStyle::SP_ComputerIcon,
            QStyle::SP_MessageBoxInformation, QStyle::SP_MessageBoxWarning,
            QStyle::SP_DesktopIcon
        };
        for (int i = 0; i < tabIcons.size() && i < ui->tabWidget->count(); ++i)
            ui->tabWidget->setTabIcon(i, classic ? QIcon() : style()->standardIcon(tabIcons.at(i)));
        themeSettings->setValue(QStringLiteral("appearance/theme"), theme);
        qInfo() << "Appearance theme changed to" << theme;
    });

    debugOutput = ui->debugTextEdit;
    ui->debugTextEdit->document()->setMaximumBlockCount(15000);
    connect(ui->debugEnableCheckBox, &QCheckBox::toggled, this, [](bool enabled) {
        if (enabled) {
            debugOutputEnabled.store(true, std::memory_order_relaxed);
            qInfo() << "Debug output enabled";
        } else {
            qInfo() << "Debug output disabled";
            debugOutputEnabled.store(false, std::memory_order_relaxed);
        }
    });
    qInstallMessageHandler(debugMessageHandler);
    qInfo() << "KodiGui debug logging started";

    const QString buildDate = QStringLiteral(__DATE__);
    const QString buildTime = QStringLiteral(__TIME__);
    ui->buildInfoText->setHtml(
        QStringLiteral("<h2>KodiGui</h2>"
                       "<p><b>Дата сборки:</b> %1</p>"
                       "<p><b>Время сборки:</b> %2</p>"
                       "<p><b>Qt:</b> %3</p>"
                       "<p><b>Компилятор:</b> %4</p>")
            .arg(buildDate.toHtmlEscaped(), buildTime.toHtmlEscaped(),
                 QString::fromLatin1(qVersion()).toHtmlEscaped(),
#if defined(__clang__)
                 QStringLiteral("Clang ") + QStringLiteral(__clang_version__)
#elif defined(__GNUC__)
                 QStringLiteral("GCC ") + QStringLiteral(__VERSION__)
#elif defined(_MSC_VER)
                 QStringLiteral("MSVC %1").arg(_MSC_VER)
#else
                 QStringLiteral("Unknown")
#endif
                 ));

    connect(kodidlpProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromLocal8Bit(kodidlpProcess->readAllStandardOutput());
        if (!output.isEmpty()) {
            qInfo().noquote() << "[kodidlp stdout]" << output.trimmed();
            ui->textEdit_info->moveCursor(QTextCursor::End);
            ui->textEdit_info->insertPlainText(output);
        }
    });
    connect(kodidlpProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString output = QString::fromLocal8Bit(kodidlpProcess->readAllStandardError());
        if (!output.isEmpty()) {
            qWarning().noquote() << "[kodidlp stderr]" << output.trimmed();
            ui->textEdit_info->moveCursor(QTextCursor::End);
            ui->textEdit_info->insertPlainText(output);
        }
    });
    connect(kodidlpProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        qInfo() << "kodidlp finished; exitCode=" << exitCode
                 << "exitStatus=" << exitStatus;
        ui->startKodidlpButton->setEnabled(true);
        ui->startKodidlpButton->setText(QStringLiteral("run"));
        ui->textEdit_info->append(
            exitStatus == QProcess::NormalExit && exitCode == 0
                ? QStringLiteral("\n[kodidlp завершился успешно]")
                : QStringLiteral("\n[kodidlp завершился с ошибкой: %1]").arg(exitCode));
    });
    connect(kodidlpProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qCritical() << "kodidlp process error:" << error
                    << kodidlpProcess->errorString();
        if (error == QProcess::FailedToStart) {
            ui->startKodidlpButton->setEnabled(true);
            ui->startKodidlpButton->setText(QStringLiteral("run"));
            ui->textEdit_info->append(
                QStringLiteral("[Не удалось запустить kodidlp: %1]")
                    .arg(kodidlpProcess->errorString()));
        }
    });


}


bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != ui->mousePadWidget)
        return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            mousePadPressPos = mouseEvent->pos();
            mousePadPressed = true;
            ui->mousePadStatusLabel->setText(QStringLiteral("Панель активна — перемещайте палец или мышь"));
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton) {
            postInputAction(QStringLiteral("back"));
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && mousePadPressed) {
            mousePadPressed = false;
            const QPoint delta = mouseEvent->pos() - mousePadPressPos;
            if (delta.manhattanLength() < 24) {
                postInputAction(QStringLiteral("select"));
                ui->mousePadStatusLabel->setText(QStringLiteral("Выбор элемента"));
            } else if (qAbs(delta.x()) >= qAbs(delta.y())) {
                postInputAction(delta.x() > 0 ? QStringLiteral("right") : QStringLiteral("left"));
                ui->mousePadStatusLabel->setText(delta.x() > 0
                    ? QStringLiteral("Вправо") : QStringLiteral("Влево"));
            } else {
                postInputAction(delta.y() > 0 ? QStringLiteral("down") : QStringLiteral("up"));
                ui->mousePadStatusLabel->setText(delta.y() > 0
                    ? QStringLiteral("Вниз") : QStringLiteral("Вверх"));
            }
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        if (wheelEvent->angleDelta().y() != 0) {
            postInputAction(wheelEvent->angleDelta().y() > 0
                ? QStringLiteral("up") : QStringLiteral("down"));
            ui->mousePadStatusLabel->setText(wheelEvent->angleDelta().y() > 0
                ? QStringLiteral("Прокрутка вверх") : QStringLiteral("Прокрутка вниз"));
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::~MainWindow()
{
    if (kodidlpProcess->state() != QProcess::NotRunning)
        kodidlpProcess->kill();

    if (pythonProcess->state() != QProcess::NotRunning) {
        pythonProcess->terminate();
        if (!pythonProcess->waitForFinished(1000))
            pythonProcess->kill();
    }
    delete ui;
}


void MainWindow::on_debugClearButton_clicked()
{
    ui->debugTextEdit->clear();
    qInfo() << "Debug log cleared";
}

void MainWindow::on_volumeSlider_valueChanged(int value)
{
    postSetVolume(value);
    qDebug() << "Значение горизонтального слайдера изменилось:" << value;
}

void MainWindow::sendJsonRpc(
    const QJsonObject &json,
    const QString &desc,
    std::function<void(const QJsonObject&)> onSuccess)
{
    QNetworkRequest request(QUrl("http://192.168.8.45:8081/jsonrpc"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QByteArray requestBody = QJsonDocument(json).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "[Kodi JSON-RPC request]" << desc << requestBody;
    QNetworkReply *reply = manager->post(request, requestBody);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            const QString errorText = reply->errorString();
            qWarning() << "[Kodi network error]" << desc << errorText;
            qWarning() << desc << "failed:" << errorText;
            if (onSuccess) {
                QJsonObject errorResponse;
                errorResponse["error"] = QJsonObject{
                    {"code", -1},
                    {"message", errorText}
                };
                onSuccess(errorResponse);
            }
            reply->deleteLater();
            return;
        }

        const QByteArray responseBody = reply->readAll();
        qInfo().noquote() << "[Kodi JSON-RPC response]" << desc << responseBody;
        QJsonObject response = QJsonDocument::fromJson(responseBody).object();

        qDebug() << desc << "OK";

        if (onSuccess)
            onSuccess(response);

        reply->deleteLater();
    });
}

// ---------------------------------------------------------------------
// Общие хелперы для Kodi JSON-RPC.
// Раньше каждый обработчик кнопки сам создавал QNetworkAccessManager,
// собирал QJsonObject и слушал QNetworkReply::finished — один и тот же
// код повторялся более десятка раз. Теперь все они сводятся к sendJsonRpc().
// ---------------------------------------------------------------------

void MainWindow::postPlayerOpenFile(const QString &file)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Player.Open";
    obj["params"] = QJsonObject{{"item", QJsonObject{{"file", file}}}};

    sendJsonRpc(obj, "Player.Open file=" + file);
}

void MainWindow::postPlayerOpenChannel(int channelId)
{
    // Kodi может аварийно завершиться, если открыть PVR-канал при
    // выключенном IPTV Simple Client. Сначала проверяем состояние аддона.
    QJsonObject check;
    check["jsonrpc"] = "2.0";
    check["id"] = rpcId++;
    check["method"] = "Addons.GetAddonDetails";
    check["params"] = QJsonObject{
        {"addonid", "pvr.iptvsimple"},
        {"properties", QJsonArray{"name", "enabled", "version"}}
    };

    sendJsonRpc(check, "Check IPTV Simple Client",
                [this, channelId](const QJsonObject &response) {
        if (response.contains("error")) {
            const QString details =
                response["error"].toObject()["message"].toString();
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client"),
                QStringLiteral("Не удалось проверить IPTV Simple Client на Kodi.%1\n\n"
                               "Воспроизведение канала отменено.")
                    .arg(details.isEmpty() ? QString() : "\n" + details));
            return;
        }

        const QJsonObject addon =
            response["result"].toObject()["addon"].toObject();
        if (addon.isEmpty() || !addon["enabled"].toBool(false)) {
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client выключен"),
                QStringLiteral("Дополнение IPTV Simple Client (pvr.iptvsimple) "
                               "не установлено или выключено.\n\n"
                               "Включите его в Kodi: Настройки → Дополнения → "
                               "Мои дополнения → Клиенты PVR → IPTV Simple Client.\n\n"
                               "Воспроизведение канала отменено, чтобы избежать "
                               "аварийного завершения Kodi."));
            return;
        }

        QJsonObject obj;
        obj["jsonrpc"] = "2.0";
        obj["id"] = rpcId++;
        obj["method"] = "Player.Open";
        obj["params"] = QJsonObject{
            {"item", QJsonObject{{"channelid", channelId}}}
        };

        sendJsonRpc(obj, QString("Player.Open channelid=%1").arg(channelId));
    });
}

void MainWindow::postPlayerStop()
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Player.Stop";
    obj["params"] = QJsonObject{{"playerid", 1}};

    sendJsonRpc(obj, "Player.Stop");
}

void MainWindow::postSetVolume(int volume)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Application.SetVolume";
    obj["params"] = QJsonObject{{"volume", volume}};

    sendJsonRpc(obj, QString("Application.SetVolume=%1").arg(volume));
}

void MainWindow::postInputAction(const QString &action)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Input.ExecuteAction";
    obj["params"] = QJsonObject{{"action", action}};

    sendJsonRpc(obj, "Input.ExecuteAction action=" + action);
}

void MainWindow::postSetAddonEnabled(const QString &addonId, const QJsonValue &enabledValue)
{
    QJsonObject obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = rpcId++;
    obj["method"] = "Addons.SetAddonEnabled";
    obj["params"] = QJsonObject{{"addonid", addonId}, {"enabled", enabledValue}};

    sendJsonRpc(obj, "Addons.SetAddonEnabled " + addonId);
}

// ---------------------------------------------------------------------
// Общий хелпер для SSH-команд "убить процесс на Pi". Раньше на каждую
// такую кнопку заводился отдельный обработчик с лямбдой executeSequence,
// внутри которой был один-единственный вызов runCommand.
// ---------------------------------------------------------------------
void MainWindow::sshKillProcess(const QString &processName)
{
    runCommand("ssh", {"pi@192.168.8.45", "killall -9 " + processName + " || true"});
}

// ---------------------------------------------------------------------
// Общий хелпер очистки текстового поля с плейсхолдером.
// ---------------------------------------------------------------------
void MainWindow::clearLineEditField(QLineEdit *edit, const QString &placeholder)
{
    edit->clear();
    edit->setPlaceholderText(placeholder);
    edit->setFocus();
}

// ---------------------------------------------------------------------
// Синхронно заливает локальный файл на Pi по SFTP через сырой TCP-сокет
// и libssh2. Раньше этот ~70-строчный блок был продублирован дословно
// в двух обработчиках (on_playIptvUrlButton_clicked и on_playMatchTvIptvButton_clicked).
// Предназначена для вызова из фонового потока (QtConcurrent::run), как и раньше.
// ---------------------------------------------------------------------
bool uploadFileViaSftp(const QString &localPath, const QString &remoteFile)
{
    const QString host = "192.168.8.45";
    const int port = 22;
    const QString user = "pi";
    const QString password = "639639";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { qDebug() << "Socket error"; return false; }

    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    struct hostent* he = gethostbyname(host.toUtf8().constData());
    if (!he) { ::close(sock); return false; }
    sin.sin_addr = *(struct in_addr*)he->h_addr;

    if (::connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) { ::close(sock); return false; }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) { ::close(sock); return false; }
    if (libssh2_session_handshake(session, sock)) { libssh2_session_free(session); ::close(sock); return false; }
    if (libssh2_userauth_password(session, user.toUtf8().constData(), password.toUtf8().constData())) {
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        ::close(sock);
        return false;
    }

    LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
    if (!sftp) { libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return false; }

    LIBSSH2_SFTP_HANDLE* sftpHandle = libssh2_sftp_open(sftp,
        remoteFile.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);

    if (!sftpHandle) { libssh2_sftp_shutdown(sftp); libssh2_session_disconnect(session, "Bye"); libssh2_session_free(session); ::close(sock); return false; }

    bool ok = false;
    QFile file(localPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        libssh2_sftp_write(sftpHandle, data.constData(), data.size());
        ok = true;
    }

    libssh2_sftp_close(sftpHandle);
    libssh2_sftp_shutdown(sftp);
    libssh2_session_disconnect(session, "Bye");
    libssh2_session_free(session);
    ::close(sock);

    if (ok)
        qDebug() << "File uploaded successfully:" << remoteFile;
    return ok;
}

void MainWindow::on_startKodidlpButton_clicked()
{
    const QString input = currentKodidlpUrl().trimmed();
    if (input.isEmpty()) {
        ui->textEdit_info->append(QStringLiteral("[Введите URL для kodidlp]"));
        return;
    }

    if (kodidlpProcess->state() != QProcess::NotRunning)
        return;

    // Перед запуском kodidlp проверяем, что IPTV Simple Client включён.
    // Это предотвращает запуск обработки URL в Kodi с выключенным PVR-клиентом.
    QJsonObject check;
    check["jsonrpc"] = "2.0";
    check["id"] = rpcId++;
    check["method"] = "Addons.GetAddonDetails";
    check["params"] = QJsonObject{
        {"addonid", "pvr.iptvsimple"},
        {"properties", QJsonArray{"name", "enabled", "version"}}
    };

    ui->textEdit_info->append(QStringLiteral("[Проверка IPTV Simple Client...]"));
    ui->startKodidlpButton->setEnabled(false);
    ui->startKodidlpButton->setText(QStringLiteral("Проверка..."));

    sendJsonRpc(check, "Check IPTV Simple Client before kodidlp",
                [this, input](const QJsonObject &response) {
        if (response.contains("error")) {
            const QString details =
                response["error"].toObject()["message"].toString();
            ui->textEdit_info->append(
                QStringLiteral("[Не удалось проверить IPTV Simple Client: %1]")
                    .arg(details.isEmpty() ? QStringLiteral("неизвестная ошибка") : details));
            ui->startKodidlpButton->setEnabled(true);
            ui->startKodidlpButton->setText(QStringLiteral("run"));
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client"),
                QStringLiteral("Не удалось проверить состояние IPTV Simple Client на Kodi.\n\n"
                               "Запуск kodidlp отменён."));
            return;
        }

        const QJsonObject addon =
            response["result"].toObject()["addon"].toObject();
        if (addon.isEmpty() || !addon["enabled"].toBool(false)) {
            ui->textEdit_info->append(
                QStringLiteral("[IPTV Simple Client выключен — запуск kodidlp отменён]"));
            ui->startKodidlpButton->setEnabled(true);
            ui->startKodidlpButton->setText(QStringLiteral("run"));
            QMessageBox::warning(
                this,
                QStringLiteral("IPTV Simple Client выключен"),
                QStringLiteral("Дополнение IPTV Simple Client (pvr.iptvsimple) "
                               "не установлено или выключено.\n\n"
                               "Включите его в Kodi: Настройки → Дополнения → "
                               "Мои дополнения → Клиенты PVR → IPTV Simple Client.\n\n"
                               "Запуск kodidlp отменён."));
            return;
        }

        ui->textEdit_info->clear();
        ui->textEdit_info->append(QStringLiteral("[IPTV Simple Client включён]"));
        ui->textEdit_info->append(QStringLiteral("[Запуск kodidlp...]"));
        ui->startKodidlpButton->setText(QStringLiteral("Выполняется..."));
        kodidlpProcess->start(QStringLiteral("kodidlp"), QStringList{input});
    });
}

void MainWindow::on_showStreamlinkInfoButton_clicked()
{
    // Получаем текст из lineEdit (с заменой vksport-ссылок на vk.ru)
    QString input = currentKodidlpUrl();

    qDebug() << input;

    QProcess process;
    // Формируем аргументы для streamlink
    QStringList arguments;
    arguments << input;

    // Указываем программу
    process.setProgram("streamlink");
    process.setArguments(arguments);

    // Запуск процесса
    qInfo() << "Starting streamlink:" << process.program() << arguments;
    process.start();

    if (!process.waitForFinished()) {
        qCritical() << "streamlink failed:" << process.errorString();
        ui->textEdit_info->setText("Ошибка выполнения процесса");
        return;
    }

    // Чтение вывода (stdout)
    QString output = process.readAllStandardOutput();
    qInfo().noquote() << "[streamlink stdout]" << output.trimmed();

    // Если нужно — читаем ошибки
    QString errorOutput = process.readAllStandardError();
    if (!errorOutput.isEmpty())
        qWarning().noquote() << "[streamlink stderr]" << errorOutput.trimmed();
    qInfo() << "streamlink finished; exitCode=" << process.exitCode();

    // Выводим в textEdit
    ui->textEdit_info->setText(output + "\n" + errorOutput);
}

QString MainWindow::currentKodidlpUrl()
{
     QString input = ui->kodidlpUrlEdit->text();

     static const QRegularExpression vksportRe(
         "^https://vksport\\.vkvideo\\.ru/(?:live|video)-(\\d+_\\d+)$");
     QRegularExpressionMatch match = vksportRe.match(input);
     if (match.hasMatch()) {
         input = "https://vk.ru/video-" + match.captured(1);
     }

     return input;
}

void MainWindow::on_playPvrChannelButton_clicked()
{
    postPlayerOpenChannel(1);
}


void MainWindow::on_stopPvrChannelButton_clicked()
{
    postPlayerStop();
}



void MainWindow::on_startKodiButton_clicked()
{
    QStringList arguments;
    arguments << "-p"
              << "639639"
              << "ssh"
              << "-o"
              << "ConnectTimeout=10"
              << "pi@192.168.8.45"
              << "nohup kodi >/dev/null 2>&1 </dev/null &";

    const bool started = QProcess::startDetached("sshpass", arguments);

    if (!started) {
        qWarning() << "Start Kodi: failed to launch sshpass";
    } else {
        qDebug() << "Start Kodi: SSH launch initiated";
    }
}



void MainWindow::on_stopKodiButton_clicked()
{

    QProcess process;
    QStringList arguments;
    arguments   << "-p"
              << "639639"
              << "ssh"
              <<  "pi@192.168.8.45"
              <<  "killall -9 kodi.bin  &";
    process.start("sshpass", arguments);
    process.waitForFinished();

}


void MainWindow::on_rebootKodiButton_clicked()
{
    QProcess process;
    QStringList arguments;
    arguments   << "-p"
              << "639639"
              << "ssh"
              <<  "pi@192.168.8.45"
              <<  "sudo reboot &";
    process.start("sshpass", arguments);
    process.waitForFinished();
}




QString  MainWindow::currentIptvUrl()
{
    QString input = ui->iptvUrlEdit->text();
    return input;
}


void MainWindow::on_playIptvUrlButton_clicked()
{
    // Берём текст из lineEdit_2
    QString input = ui->iptvUrlEdit->text();
    QString filePath = "/tmp/list.m3u";

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file) << "#EXTM3U\n" << "#EXTINF:-1,My Channel Name\n" << input << "\n";
    file.close();
    qDebug() << "Temporary M3U created at" << filePath;

    QPointer<MainWindow> safeThis(this);

    // 2️⃣ Останавливаем плеер
    QJsonObject stop;
    stop["jsonrpc"] = "2.0";
    stop["method"] = "Player.Stop";
    stop["params"] = QJsonObject{{"playerid", 1}};
    stop["id"] = rpcId++;

    sendJsonRpc(stop, "Player.Stop", [safeThis, filePath](const QJsonObject &){
        if(!safeThis) return;

        // 3️⃣ Асинхронный SFTP upload
        [[maybe_unused]] auto uploadFuture = QtConcurrent::run([safeThis, filePath]() {
            if (!uploadFileViaSftp(filePath, "/var/www/html/list.m3u"))
                return;

            // 4️⃣ JSON-RPC в основном потоке
            if (!safeThis) return;
            QMetaObject::invokeMethod(safeThis, [safeThis]() {

                // Включаем PVR
                QJsonObject enable;
                enable["jsonrpc"] = "2.0";
                enable["method"] = "Addons.SetAddonEnabled";
                enable["params"] = QJsonObject{{"addonid","pvr.iptvsimple"}, {"enabled", true}};
                enable["id"] = safeThis->rpcId++;

                safeThis->sendJsonRpc(enable, "Enable PVR", [safeThis](const QJsonObject &){
                    if(!safeThis) return;

                    // Запуск PVR scan
                    QJsonObject scan;
                    scan["jsonrpc"]="2.0";
                    scan["method"]="PVR.Scan";
                    scan["id"]=safeThis->rpcId++;
                    safeThis->sendJsonRpc(scan,"PVR scan");

                    // Ждём появления каналов
                    auto waitForChannels = std::make_shared<std::function<void(int)>>();
                    *waitForChannels = [safeThis, waitForChannels](int attemptsLeft){
                        if(!safeThis) return;
                        QJsonObject getGroups;
                        getGroups["jsonrpc"]="2.0";
                        getGroups["method"]="PVR.GetChannelGroups";
                        getGroups["id"]=safeThis->rpcId++;

                        safeThis->sendJsonRpc(getGroups,"Get Channel Groups",[safeThis, attemptsLeft, waitForChannels](const QJsonObject &resp){
                            if(!safeThis) return;
                            QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                            if(!groups.isEmpty()) {
                                QString groupId = groups.first().toObject()["channelgroupid"].toString();

                                QJsonObject getChannels;
                                getChannels["jsonrpc"]="2.0";
                                getChannels["method"]="PVR.GetChannels";
                                getChannels["params"]=QJsonObject{
                                    {"channelgroupid", groupId},
                                    {"properties", QJsonArray{"channelnumber","label"}}
                                };
                                getChannels["id"]=safeThis->rpcId++;

                                safeThis->sendJsonRpc(getChannels,"Get Channels",[safeThis](const QJsonObject &resp){
                                    if(!safeThis) return;
                                    QJsonArray channels = resp["result"].toObject()["channels"].toArray();
                                    int channelId = 1;
                                    if(!channels.isEmpty()){
                                        channelId = channels.first().toObject()["channelid"].toInt();
                                        qDebug()<<"Opening first PVR channel:"<<channels.first().toObject()["label"].toString();
                                    }
                                    QJsonObject play;
                                    play["jsonrpc"]="2.0";
                                    play["method"]="Player.Open";
                                    play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",channelId}}}};
                                    play["id"]=safeThis->rpcId++;
                                    safeThis->sendJsonRpc(play,"Player.Open");
                                });
                            } else if(attemptsLeft>1){
                                QTimer::singleShot(1000,safeThis,[waitForChannels,attemptsLeft](){(*waitForChannels)(attemptsLeft-1);});
                            } else {
                                QJsonObject play;
                                play["jsonrpc"]="2.0";
                                play["method"]="Player.Open";
                                play["params"]=QJsonObject{{"item", QJsonObject{{"channelid",1}}}};
                                play["id"]=safeThis->rpcId++;
                                safeThis->sendJsonRpc(play,"Player.Open");
                            }
                        });
                    };
                    (*waitForChannels)(3);
                });

            }, Qt::QueuedConnection);

        });

    });
}


void MainWindow::on_playYoutubeFileButton_clicked()
{
    postPlayerOpenFile("/home/pi/kodigui/yt.mp4");
}


void runCommand(const QString &command, const QStringList &args) {
    // Неблокирующий запуск: UI не замирает
    const bool ok = QProcess::startDetached(command, args);
    if (!ok) {
        qWarning() << "Не удалось запустить:" << command << args;
    } else {
        qDebug() << "Команда запущена (detached):" << command << args;
    }
}


void MainWindow::on_startYoutubeDownloadButton_clicked()
{
    const QString user = "pi@192.168.8.45";
    const QString input = currentYoutubeUrl().trimmed();

    if (input.isEmpty()) {
        QMessageBox::warning(this, "YouTube", "Введите live URL.");
        return;
    }

    // Экранируем URL для удалённой shell-команды.
    auto shellQuote = [](const QString &value) {
        QString escaped = value;
        escaped.replace("'", "'\\''");
        return "'" + escaped + "'";
    };

    // youtubeUnblock достаточно запустить один раз. Повторный restart
    // непосредственно перед загрузкой не нужен и может оборвать сеть.
    const QString remoteCmd =
        "sudo systemctl start youtubeUnblock && "
        "mkdir -p \"$HOME/kodigui\" && "
        "killall -9 yt-dlp 2>/dev/null || true; "
        "killall -9 ffmpeg 2>/dev/null || true; "
        "rm -f \"$HOME/kodigui/yt.mp4\" \"$HOME/kodigui/yt.part\"; "
        "printf '\\n===== yt-dlp start: %s =====\\n' \"$(date -Is)\" "
        ">> \"$HOME/kodigui/yt-dlp.log\"; "
        "setsid -f \"$HOME/.local/bin/yt-dlp\" --verbose --newline "
        "--js-runtimes deno -f '229+234' --merge-output-format mp4 "
        "--no-part --force-overwrites "
        "--retries infinite --fragment-retries infinite --retry-sleep 5 "
        + shellQuote(input) +
        " -o \"$HOME/kodigui/yt\" "
        ">> \"$HOME/kodigui/yt-dlp.log\" 2>&1 < /dev/null";

    runCommand("ssh", {user, remoteCmd});

    // Через 50 секунд открываем файл в Kodi, как и раньше.
    QTimer::singleShot(50000, this, [this]() {
        qDebug() << "Прошло 50 секунд, открываем /home/pi/kodigui/yt.mp4";
        postPlayerOpenFile("/home/pi/kodigui/yt.mp4");
    });
}

void MainWindow::on_startYoutubeStreamlinkButton_clicked()
{
    const QString user = "pi@192.168.8.45";
    const QString input = currentYoutubeUrl().trimmed();

    if (input.isEmpty()) {
        QMessageBox::warning(this, "YouTube", "Введите live URL.");
        return;
    }

    auto shellQuote = [](const QString &value) {
        QString escaped = value;
        escaped.replace("'", "'\\''");
        return "'" + escaped + "'";
    };

    const QString remoteCmd =
        "mkdir -p \"$HOME/kodigui\"; "
        "rm -f \"$HOME/kodigui/yt.mp4\"; "
        "printf '\\n===== Streamlink start: %s =====\\n' \"$(date -Is)\" "
        ">> \"$HOME/kodigui/streamlink.log\"; "
        "nohup setsid \"$HOME/.local/bin/streamlink\" "
        + shellQuote(input) +
        " 360p -f -o \"$HOME/kodigui/yt.mp4\" "
        ">> \"$HOME/kodigui/streamlink.log\" 2>&1 < /dev/null & "
        "echo $! > \"$HOME/kodigui/streamlink.pid\"";

    runCommand("ssh", {user, remoteCmd});
}

void MainWindow::on_killYoutubeStreamlinkButton_clicked()
{
    // Останавливаем только тот Streamlink, который был запущен с вкладки YouTube.
    // PID проверяется по командной строке; отдельная сессия позволяет завершить
    // также дочерние процессы, не затрагивая Streamlink на других вкладках.
    const QString remoteCmd =
        "pidfile=\"$HOME/kodigui/streamlink.pid\"; "
        "if [ -s \"$pidfile\" ]; then "
        "  p=$(cat \"$pidfile\"); "
        "  case \"$p\" in ''|*[!0-9]*) p='';; esac; "
        "  if [ -n \"$p\" ] && [ \"$p\" -gt 1 ] 2>/dev/null && "
        "     [ -r \"/proc/$p/cmdline\" ] && "
        "     tr '\\0' ' ' < \"/proc/$p/cmdline\" | grep -Fq \"$HOME/.local/bin/streamlink\"; then "
        "    pgid=$(ps -o pgid= -p \"$p\" 2>/dev/null | tr -d ' '); "
        "    if [ \"$pgid\" = \"$p\" ]; then "
        "      kill -TERM -- \"-$p\" 2>/dev/null || true; "
        "      sleep 2; "
        "      kill -0 -- \"-$p\" 2>/dev/null && kill -KILL -- \"-$p\" 2>/dev/null || true; "
        "    else "
        "      kill -TERM \"$p\" 2>/dev/null || true; "
        "    fi; "
        "  fi; "
        "  rm -f \"$pidfile\"; "
        "fi";

    runCommand("ssh", {"pi@192.168.8.45", remoteCmd});
}

void MainWindow::on_closeYoutubePopupButton_clicked()
{
    postInputAction("back");
}


QString MainWindow::currentYoutubeUrl()
{
    QString input = ui->youtubeUrlEdit->text();
    return input;
}


void MainWindow::on_stopYoutubePlaybackButton_clicked()
{
    postPlayerStop();
}


void MainWindow::on_stopYtDlpButton_clicked()
{
    sshKillProcess("yt-dlp");
}


void MainWindow::on_stopFfmpegButton_clicked()
{
    sshKillProcess("ffmpeg");
}


void MainWindow::on_toggleIptvSimpleClientButton_clicked()
{
    postSetAddonEnabled("pvr.iptvsimple", QJsonValue("toggle"));
}


void MainWindow::on_sendBackActionButton_clicked()
{
    postInputAction("back");
}


void MainWindow::on_clearUrlFieldsButton_clicked()
{
    clearLineEditField(ui->iptvUrlEdit, "Введите URL...");
    clearLineEditField(ui->kodidlpUrlEdit, "Введите URL...");
}




//получение урл для матч тв
void MainWindow::on_captureMatchTvUrlButton_clicked()
{

QUrl url("https://matchtv.ru/on-air");

    // Создаём и показываем встроенный плеер
    PlayerWindow *player = new PlayerWindow(url, this);
    player->setAttribute(Qt::WA_DeleteOnClose); // автоудаление при закрытии
    player->show();
    connect(player, &PlayerWindow::urlCaptured,
            this, [&](const QUrl& capturedUrl){
                ui->matchTvUrlEdit->setText(capturedUrl.toString()); // Обращаемся через указатель
            });
    qDebug() << "Открыт встроенный плеер:" << url.toString();


}
void MainWindow::on_playMatchTvIptvButton_clicked()
{
    QString input = ui->matchTvUrlEdit->text();
    QString filePath = "/tmp/list.m3u";

    if (input.trimmed().isEmpty()) {
        qWarning() << "Match TV: URL is empty — press \"Получить URL\" first";
        return;
    }

    // 1️⃣ Создаём M3U
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "Cannot create M3U file";
        return;
    }
    QTextStream(&file)
        << "#EXTM3U\n"
        << "#EXTINF:-1,My Channel Name\n"
        << input << "\n";
    file.close();

    qDebug() << "Temporary M3U created at" << filePath;

    QPointer<MainWindow> safeThis(this);

    // Короткая обёртка над JSON-RPC: метод + params + описание + колбэк.
    auto rpc = [safeThis](const QString &method, const QJsonObject &params,
                          const QString &desc,
                          std::function<void(const QJsonObject &)> cb) {
        if (!safeThis) return;
        QJsonObject o;
        o["jsonrpc"] = "2.0";
        o["method"]  = method;
        if (!params.isEmpty())
            o["params"] = params;
        o["id"] = safeThis->rpcId++;
        safeThis->sendJsonRpc(o, desc, cb);
    };

    // ---- Шаг 6: ждём канал в PVR и запускаем его ------------------------
    auto pollChannel = std::make_shared<std::function<void(int)>>();
    *pollChannel = [safeThis, rpc, pollChannel](int attemptsLeft) {
        if (!safeThis) return;
        auto retry = [safeThis, pollChannel, attemptsLeft](const char *why) {
            if (attemptsLeft > 1) {
                QTimer::singleShot(1500, safeThis, [pollChannel, attemptsLeft]() {
                    (*pollChannel)(attemptsLeft - 1);
                });
            } else {
                qWarning() << "Match TV: giving up:" << why;
            }
        };

        rpc("PVR.GetChannelGroups", QJsonObject{{"channeltype", "tv"}}, "Get Channel Groups",
            [safeThis, rpc, retry](const QJsonObject &resp) {
                if (!safeThis) return;
                if (resp.contains("error")) {
                    qWarning() << "Match TV: GetChannelGroups error:" << resp["error"];
                    retry("GetChannelGroups error");
                    return;
                }
                QJsonArray groups = resp["result"].toObject()["channelgroups"].toArray();
                if (groups.isEmpty()) {
                    retry("no channel groups");
                    return;
                }
                // channelgroupid в ответе Kodi — число, не строка.
                QJsonValue groupId = groups.first().toObject()["channelgroupid"];

                rpc("PVR.GetChannels",
                    QJsonObject{{"channelgroupid", groupId},
                                {"properties", QJsonArray{"channel"}}},
                    "Get Channels",
                    [safeThis, rpc, retry](const QJsonObject &chResp) {
                        if (!safeThis) return;
                        if (chResp.contains("error")) {
                            qWarning() << "Match TV: GetChannels error:" << chResp["error"];
                            retry("GetChannels error");
                            return;
                        }
                        QJsonArray channels = chResp["result"].toObject()["channels"].toArray();
                        if (channels.isEmpty()) {
                            retry("no channels");
                            return;
                        }
                        int channelId = channels.first().toObject()["channelid"].toInt();
                        qDebug() << "Match TV: opening channel"
                                 << channels.first().toObject()["channel"].toString();

                        rpc("Player.Open",
                            QJsonObject{{"item", QJsonObject{{"channelid", channelId}}}},
                            "Play channel",
                            [](const QJsonObject &playResp) {
                                if (playResp.contains("error"))
                                    qWarning() << "Match TV: Player.Open error:" << playResp["error"];
                                else
                                    qDebug() << "Match TV: playback started";
                            });
                    });            });
    };

    // ---- Шаг 5: ждём, пока PVR-менеджер Kodi реально поднимется ----------
    // Любые запросы к PVR (GetChannelGroups и т.п.) в момент, когда клиент
    // ещё стартует после Enable, могут уронить Kodi. Поэтому сначала
    // опрашиваем PVR.GetProperties("available").
    auto waitPvr = std::make_shared<std::function<void(int)>>();
    *waitPvr = [safeThis, rpc, waitPvr, pollChannel](int attemptsLeft) {
        if (!safeThis) return;
        rpc("PVR.GetProperties", QJsonObject{{"properties", QJsonArray{"available"}}},
            "PVR.GetProperties",
            [safeThis, waitPvr, pollChannel, attemptsLeft](const QJsonObject &resp) {
                if (!safeThis) return;
                const bool available =
                    !resp.contains("error") && resp["result"].toObject()["available"].toBool();
                if (available) {
                    // Даём клиенту ещё секунду дочитать плейлист.
                    QTimer::singleShot(1500, safeThis, [pollChannel]() { (*pollChannel)(10); });
                } else if (attemptsLeft > 1) {
                    QTimer::singleShot(1000, safeThis, [waitPvr, attemptsLeft]() {
                        (*waitPvr)(attemptsLeft - 1);
                    });
                } else {
                    qWarning() << "Match TV: PVR did not become available, aborting";
                }
            });
    };

    // ---- Шаг 4: Enable PVR-клиента (после паузы) -------------------------
    auto enablePvr = [safeThis, rpc, waitPvr]() {
        rpc("Addons.SetAddonEnabled",
            QJsonObject{{"addonid", "pvr.iptvsimple"}, {"enabled", true}},
            "Enable PVR",
            [safeThis, waitPvr](const QJsonObject &enableResp) {
                if (!safeThis) return;
                if (enableResp.contains("error")) {
                    qWarning() << "Match TV: Enable PVR error:" << enableResp["error"];
                    return;
                }
                // PVR.Scan больше не вызываем: у iptvsimple нет «скана», он
                // отвечает -32100, а сам вызов во время старта клиента опасен.
                QTimer::singleShot(3000, safeThis, [waitPvr]() { (*waitPvr)(20); });
            });
    };

    // ---- Шаг 3: Disable PVR-клиента, затем длинная пауза -----------------
    // Disable и Enable подряд без паузы роняют Kodi: PVR-менеджер не успевает
    // остановить клиент. Ждём 4 секунды между ними.
    auto disablePvr = [safeThis, rpc, enablePvr]() {
        rpc("Addons.SetAddonEnabled",
            QJsonObject{{"addonid", "pvr.iptvsimple"}, {"enabled", false}},
            "Disable PVR",
            [safeThis, enablePvr](const QJsonObject &disableResp) {
                if (!safeThis) return;
                if (disableResp.contains("error"))
                    qWarning() << "Match TV: Disable PVR error:" << disableResp["error"];
                QTimer::singleShot(4000, safeThis, [enablePvr]() { enablePvr(); });
            });
    };

    // ---- Шаги 1-2: Stop плеера → SFTP upload → Disable ------------------
    rpc("Player.Stop", QJsonObject{{"playerid", 1}}, "Player.Stop",
        [safeThis, filePath, disablePvr](const QJsonObject &) {
            if (!safeThis) return;
            [[maybe_unused]] auto uploadFuture = QtConcurrent::run([safeThis, filePath, disablePvr]() {
                if (!uploadFileViaSftp(filePath, "/var/www/html/list.m3u")) {
                    qWarning() << "Match TV: SFTP upload failed, aborting";
                    return;
                }
                if (!safeThis) return;
                QMetaObject::invokeMethod(
                    safeThis,
                    [safeThis, disablePvr]() {
                        if (!safeThis) return;
                        // Плеер только что остановлен — даём Kodi секунду
                        // освободить ресурсы, прежде чем трогать аддон.
                        QTimer::singleShot(1000, safeThis, [disablePvr]() { disablePvr(); });
                    },
                    Qt::QueuedConnection);
            });
        });
}

void MainWindow::on_playMatchTvFileButton_clicked()
{
    qInfo() << "Match TV: manually opening kodigui/match.mp4 in Kodi";
    postPlayerOpenFile("kodigui/match.mp4");
}

void MainWindow::on_startMatchTvStreamlinkButton_clicked()
{
    const QString input = ui->matchTvUrlEdit->text().trimmed();
    if (input.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Матч ТВ"),
                             QStringLiteral("Сначала нажмите «Получить URL»."));
        return;
    }

    const QUrl streamUrl(input);
    if (!streamUrl.isValid() ||
        (streamUrl.scheme() != QStringLiteral("http") &&
         streamUrl.scheme() != QStringLiteral("https"))) {
        QMessageBox::warning(this, QStringLiteral("Матч ТВ"),
                             QStringLiteral("Получен некорректный HTTP(S) URL."));
        return;
    }

    // Передаём URL как один аргумент shell-команды, не позволяя символам
    // из ссылки интерпретироваться оболочкой на Raspberry Pi.
    QString quotedUrl = input;
    quotedUrl.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    quotedUrl.prepend(QLatin1Char('\''));
    quotedUrl.append(QLatin1Char('\''));

    const QString remoteCommand =
        "mkdir -p \"$HOME/kodigui\"; "
        // Удаляем предыдущий результат сразу при нажатии кнопки,
        // до остановки старого Streamlink и запуска новой загрузки.
        "rm -f \"$HOME/kodigui/match.mp4\"; "
        "if [ -s \"$HOME/kodigui/matchtv-streamlink.pid\" ]; then "
        "  old_pid=$(cat \"$HOME/kodigui/matchtv-streamlink.pid\"); "
        "  kill \"$old_pid\" 2>/dev/null || true; "
        "fi; "
        "pkill -x streamlink 2>/dev/null || true; "
        "sleep 1; "
        "nohup \"$HOME/.local/bin/streamlink\" " + quotedUrl +
        " worst -f -o \"$HOME/kodigui/match.mp4\" "
        "> \"$HOME/kodigui/matchtv-streamlink.log\" 2>&1 < /dev/null & "
        "echo $! > \"$HOME/kodigui/matchtv-streamlink.pid\"";

    qInfo() << "Match TV: starting Streamlink on Kodi host for URL" << streamUrl.host();
    ui->textBrowseronairnow->append(
        QStringLiteral("Запуск Streamlink на Kodi-сервере…"));

    runCommand("ssh", {"pi@192.168.8.45", remoteCommand});

    // Даём Streamlink время создать выходной файл, затем открываем его в Kodi.
    QTimer::singleShot(3000, this, [this]() {
        qInfo() << "Match TV: opening kodigui/match.mp4 in Kodi";
        postPlayerOpenFile("kodigui/match.mp4");
        ui->textBrowseronairnow->append(
            QStringLiteral("Команда Streamlink отправлена. Файл: $HOME/kodigui/match.mp4; "
                           "журнал: $HOME/kodigui/matchtv-streamlink.log"));
    });
}


void MainWindow::on_getonairnow_clicked()
{
 ui->textBrowseronairnow->clear();

    // 1. Читаем скрипт из ресурсов
    QFile src(":/match_now.py");
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->textBrowseronairnow->setHtml("<font color='red'>Ошибка: не найден скрипт<br>:/scripts/match_now.py</font>");
        return;
    }

    // 2. Создаём временный файл с правильным именем и правами (важно для FreeBSD!)
    QString scriptPath = QDir::tempPath() + "/kodigui_match_now.py";
    {
        QFile temp(scriptPath);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ui->textBrowseronairnow->append("Не могу записать временный файл в /tmp");
            return;
        }
        temp.write(src.readAll());
        temp.setPermissions(QFile::ReadOwner  | QFile::WriteOwner | QFile::ExeOwner |
                            QFile::ReadUser   | QFile::WriteUser  | QFile::ExeUser);
        temp.close();
    }

    // 3. Используем точно тот Python, который у тебя есть
    QString pythonCmd = "/usr/local/bin/python3.11";

    ui->textBrowseronairnow->append("<i>Запуск матча...</i>");
    ui->textBrowseronairnow->append("<small>" + pythonCmd + " " + scriptPath + "</small><hr>");

    // 4. Настраиваем процесс
    pythonProcess->setProcessChannelMode(QProcess::MergedChannels);

    // Живой вывод в textBrowseronairnow
    disconnect(pythonProcess, &QProcess::readyReadStandardOutput, nullptr, nullptr); // на всякий
    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        QString out = pythonProcess->readAllStandardOutput();
        if (!out.isEmpty())
            qInfo().noquote() << "[MatchTV Python stdout]" << out.trimmed();
        ui->textBrowseronairnow->append(out.trimmed());
    });

    // 5. По завершении — удаляем файл и пишем статус
      pythonProcess->disconnect(); // чистим все старые сигналы

    connect(pythonProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = pythonProcess->readAllStandardOutput().trimmed();
        if (!out.isEmpty())
            qInfo().noquote() << "[MatchTV Python stdout]" << out;
        ui->textBrowseronairnow->append(out);
    });

    connect(pythonProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = pythonProcess->readAllStandardError().trimmed();
        if (!err.isEmpty())
            qWarning().noquote() << "[MatchTV Python stderr]" << err;
        ui->textBrowseronairnow->append("<font color='red'>" + err.toHtmlEscaped() + "</font>");
    });

    connect(pythonProcess, &QProcess::finished, this, [this, scriptPath](int exitCode) {
        qInfo() << "MatchTV Python finished; exitCode=" << exitCode;
        if (exitCode == 0)
            ui->textBrowseronairnow->append("<hr><b><font color='green'>Матч успешно обработан</font></b>");
        else
            ui->textBrowseronairnow->append(QString("<hr><b><font color='red'>Ошибка (код %1)</font></b>").arg(exitCode));
        QFile::remove(scriptPath);
    });

    qInfo() << "Starting MatchTV Python script:" << pythonCmd << scriptPath;
    pythonProcess->start(pythonCmd, QStringList() << scriptPath);
}

void MainWindow::on_playDrmStreamButton_clicked()
{
    postPlayerOpenFile("kodigui/drm/hls/master.m3u8");
}

void MainWindow::on_stopDrmPlaybackButton_clicked()
{
    postPlayerStop();
}


void MainWindow::on_startDrmDownloadButton_clicked()
{
    const QString user = "pi@192.168.8.45";
    QString input = currentDrmUrl().trimmed();
    QString keydrm = currentDrmKey().trimmed();

    // Если хотя бы одно поле пустое, пытаемся взять недостающие значения
    // из ~/Downloads/widevine_key.txt: первая строка — URL, вторая — key.
    if (input.isEmpty() || keydrm.isEmpty()) {
        const QString keyFilePath =
            QDir::homePath() + "/Downloads/widevine_key.txt";
        QFile keyFile(keyFilePath);

        if (!keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "DRM",
                                 "Не удалось открыть файл:\n" + keyFilePath);
            return;
        }

        QTextStream stream(&keyFile);
        const QStringList lines = stream.readAll().split(
            QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
        keyFile.close();

        if (lines.size() < 2) {
            QMessageBox::warning(this, "DRM",
                                 "В файле должны быть две строки:\n"
                                 "1 — DRM URL\n2 — DRM key");
            return;
        }

        if (input.isEmpty()) {
            input = lines.at(0).trimmed();
            ui->lineEdit_drm->setText(input);
        }
        if (keydrm.isEmpty()) {
            keydrm = lines.at(1).trimmed();
            ui->lineEdit_drm_key->setText(keydrm);
        }
    }

    if (input.isEmpty() || keydrm.isEmpty()) {
        QMessageBox::warning(this, "DRM",
                             "DRM URL или key не заполнены.");
        return;
    }

    // Все операции подготовки и запуска выполняются одной SSH-командой.
    // Это исключает гонку между несколькими независимыми startDetached().
    auto shellQuote = [](const QString &value) {
        QString escaped = value;
        escaped.replace("'", "'\\''");
        return "'" + escaped + "'";
    };

    // Dynamic HLS publisher: publishes original decrypted fMP4 fragments without remuxing.
    const QString assembleScriptBody = QString::fromUtf8(R"DRM_HLS_SCRIPT(
#!/bin/bash
set -u
LOG="$HOME/kodigui/drm-mux.log"
ts() { date +%H:%M:%S; }
echo "[$(ts)] Dynamic HLS publisher started" >> "$LOG"
TASK_ROOT="$HOME/kodigui/drm/tmp/drm"
V=""; A=""; VI=""; AI=""
for i in $(seq 1 120); do
  # N_m3u8DL-RE stores live tracks under <tmp-dir>/<save-name>/<track>.
  # Discover recursively because track folder names vary between releases.
  TRACKS=$(python3 - "$TASK_ROOT" <<'PY_TRACK_DISCOVERY'
import os, pathlib, subprocess, sys
base = pathlib.Path(sys.argv[1])
video = audio = ""
for current, dirs, names in os.walk(base):
    here = pathlib.Path(current)
    init = next((here / n for n in ("_init_dec.mp4", "_init.mp4")
                 if (here / n).is_file()), None)
    if init is None or not any(n.endswith(".m4s") for n in names):
        continue
    label = here.name.lower()
    kind = ""
    if any(token in label for token in ("video", "avc1", "h264")):
        kind = "video"
    elif any(token in label for token in ("audio", "mp4a", "aac")):
        kind = "audio"
    else:
        try:
            probe = subprocess.run(
                ["ffprobe", "-v", "error", "-show_entries", "stream=codec_type",
                 "-of", "csv=p=0", str(init)],
                capture_output=True, text=True, timeout=5)
            media = set(probe.stdout.lower().split())
            if "video" in media:
                kind = "video"
            elif "audio" in media:
                kind = "audio"
        except (OSError, subprocess.SubprocessError):
            pass
    if kind == "video" and not video:
        video = current
    elif kind == "audio" and not audio:
        audio = current
print(video)
print(audio)
PY_TRACK_DISCOVERY
)
  V=$(printf '%s\n' "$TRACKS" | sed -n '1p')
  A=$(printf '%s\n' "$TRACKS" | sed -n '2p')
  if [ -n "$V" ] && [ -n "$A" ]; then
    VI=""; AI=""
    for candidate in "$V/_init_dec.mp4" "$V/_init.mp4"; do
      if [ -s "$candidate" ]; then VI="$candidate"; break; fi
    done
    for candidate in "$A/_init_dec.mp4" "$A/_init.mp4"; do
      if [ -s "$candidate" ]; then AI="$candidate"; break; fi
    done
    if [ -n "$VI" ] && [ -n "$AI" ]; then break; fi
  fi
  sleep 1
done
if [ -z "$V" ] || [ -z "$A" ] || [ -z "$VI" ] || [ -z "$AI" ]; then
  echo "[$(ts)] ERROR: live track folders/init segments not found (task_root=$TASK_ROOT video=$V audio=$A)" >> "$LOG"
  echo "[$(ts)] DRM output tree:" >> "$LOG"
  find "$HOME/kodigui/drm" -maxdepth 5 -type f 2>/dev/null | head -n 120 >> "$LOG"
  echo "[$(ts)] Downloader log tail:" >> "$LOG"
  tail -n 50 "$HOME/kodigui/drm-download.log" 2>/dev/null >> "$LOG"
  exit 1
fi
mkdir -p "$HOME/kodigui/drm/hls"
echo "[$(ts)] Found video=$V init=$(basename "$VI"); audio=$A init=$(basename "$AI")" >> "$LOG"
LAST_V=0; LAST_A=0
while true; do
  dp=""
  [ -s "$HOME/kodigui/drm_download.pid" ] && dp=$(cat "$HOME/kodigui/drm_download.pid" 2>/dev/null || true)
  finished=0
  if [ -z "$dp" ] || ! kill -0 "$dp" 2>/dev/null; then finished=1; fi
  python3 - "$V" "$A" "$HOME/kodigui/drm" "$finished" <<'PY_BUILDER'
import glob, math, os, pathlib, sys, tempfile, time
vdir, adir, root, finished = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3]), sys.argv[4] == '1'
out = root / 'hls'
out.mkdir(parents=True, exist_ok=True)

def files(directory, timescale):
    # Video and audio are independent HLS renditions. Do not force a
    # one-to-one match: their fragment boundaries/counts may legitimately differ.
    result = []
    for path in glob.glob(os.path.join(directory, '*.m4s')):
        stem = os.path.splitext(os.path.basename(path))[0]
        try:
            ticks = int(stem.split('_', 1)[0])
        except ValueError:
            continue
        result.append((ticks / timescale, path))
    return sorted(result, key=lambda item: item[0])

def stable(path):
    try:
        first = os.stat(path)
        time.sleep(0.12)
        second = os.stat(path)
        return (first.st_size > 0 and first.st_size == second.st_size
                and first.st_mtime_ns == second.st_mtime_ns)
    except OSError:
        return False

def stable_tail(entries):
    # Never publish the fragment that the downloader is still writing.
    while len(entries) >= 2 and not stable(entries[-1][1]):
        entries.pop()
    return entries

def atomic_write(path, data):
    fd, tmp = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=str(path.parent))
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass

video_entries = stable_tail(files(vdir, 600))
audio_entries = stable_tail(files(adir, 48000))
if len(video_entries) < 2 or len(audio_entries) < 2:
    sys.exit(0)

# Keep a larger safety margin behind the live edge. Kodi only sees completed
# fragments; the downloader can finish writing upcoming fragments undisturbed.
LIVE_HOLD_BACK_SEGMENTS = 5
LIVE_WINDOW_SEGMENTS = 90
STARTUP_BUFFER_SEGMENTS = 5

def make_playlist(entries, directory, done):
    visible_end = len(entries) if done else max(0, len(entries) - LIVE_HOLD_BACK_SEGMENTS)
    minimum_visible = 2 if done else STARTUP_BUFFER_SEGMENTS
    if visible_end < minimum_visible:
        return None

    start = max(0, visible_end - LIVE_WINDOW_SEGMENTS)
    selected = entries[start:visible_end]
    init_candidates = [directory / '_init_dec.mp4', directory / '_init.mp4']
    init_path = next((path for path in init_candidates
                      if path.is_file() and path.stat().st_size > 0), None)
    if init_path is None:
        raise RuntimeError(f'init segment missing in {directory}')

    durations = []
    for i, (timestamp, _) in enumerate(selected):
        absolute_index = start + i
        if absolute_index + 1 < len(entries):
            duration = entries[absolute_index + 1][0] - timestamp
        elif len(selected) > 1:
            duration = selected[-1][0] - selected[-2][0]
        else:
            duration = 3.2
        if not (0.1 <= duration <= 30):
            duration = 3.2
        durations.append(duration)

    target = max(1, math.ceil(max(durations)))
    rel_dir = os.path.relpath(directory, out)
    lines = [
        '#EXTM3U',
        '#EXT-X-VERSION:7',
        f'#EXT-X-TARGETDURATION:{target}',
        f'#EXT-X-MEDIA-SEQUENCE:{start}',
        f'#EXT-X-MAP:URI="{rel_dir}/{init_path.name}"'
    ]
    for (_, path), duration in zip(selected, durations):
        lines.extend([f'#EXTINF:{duration:.3f},',
                      f'{rel_dir}/{os.path.basename(path)}'])
    if done:
        lines.append('#EXT-X-ENDLIST')
    return '\n'.join(lines) + '\n'

video_playlist = make_playlist(video_entries, pathlib.Path(vdir), finished)
audio_playlist = make_playlist(audio_entries, pathlib.Path(adir), finished)
if video_playlist is None or audio_playlist is None:
    sys.exit(0)

atomic_write(out / 'video.m3u8', video_playlist)
atomic_write(out / 'audio.m3u8', audio_playlist)
master = ('#EXTM3U\n#EXT-X-VERSION:7\n'
          '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="AAC",DEFAULT=YES,AUTOSELECT=YES,URI="audio.m3u8"\n'
          '#EXT-X-STREAM-INF:BANDWIDTH=700000,CODECS="avc1.4D401E,mp4a.40.2",AUDIO="audio"\nvideo.m3u8\n')
atomic_write(out / 'master.m3u8', master)
print(f'published video={len(video_entries)} audio={len(audio_entries)} '
      f'fragments; holdback={0 if finished else LIVE_HOLD_BACK_SEGMENTS}; '
      f'finished={finished}')
PY_BUILDER
  rc=$?
  if [ "$rc" -ne 0 ]; then echo "[$(ts)] ERROR: playlist publisher exit=$rc" >> "$LOG"; fi
  vc=$(find "$V" -maxdepth 1 -name '*.m4s' 2>/dev/null | wc -l)
  ac=$(find "$A" -maxdepth 1 -name '*.m4s' 2>/dev/null | wc -l)
  if [ "$vc" -ne "$LAST_V" ] || [ "$ac" -ne "$LAST_A" ]; then
    echo "[$(ts)] HLS updated: video=$vc audio=$ac" >> "$LOG"; LAST_V=$vc; LAST_A=$ac
  fi
  if [ "$finished" -eq 1 ]; then echo "[$(ts)] Downloader finished; final HLS playlist published" >> "$LOG"; break; fi
  sleep 1
done
echo "[$(ts)] HLS publisher finished" >> "$LOG"
)DRM_HLS_SCRIPT");
    // Уникальный маркер не позволяет принять плейлист от предыдущего запуска.
    const QString runId = QString::number(QDateTime::currentMSecsSinceEpoch());

    const QString remoteCmd =
        // Останавливаем старые процессы, затем только после этого удаляем старые файлы.
        "mkdir -p \"$HOME/kodigui\"; "
        "if [ -s \"$HOME/kodigui/drm_mux.pid\" ]; then "
        "  old_mux=$(cat \"$HOME/kodigui/drm_mux.pid\"); "
        "  kill -TERM -- -\"$old_mux\" 2>/dev/null || true; "
        "fi; "
        "if [ -s \"$HOME/kodigui/drm_download.pid\" ]; then "
        "  old_dl=$(cat \"$HOME/kodigui/drm_download.pid\"); "
        "  kill -TERM \"$old_dl\" 2>/dev/null || true; "
        "fi; "
        "killall -9 N_m3u8DL-RE 2>/dev/null || true; "
        "sleep 1; "
        "rm -f \"$HOME/kodigui/drm_mux.pid\" \"$HOME/kodigui/drm_download.pid\" "
        "      \"$HOME/kodigui/drm-assemble.sh\"; "
        "rm -rf \"$HOME/kodigui/drm\"; "
        "mkdir -p \"$HOME/kodigui/drm/hls\"; "
        "printf '%s\\n' " + shellQuote(runId) + " > \"$HOME/kodigui/drm/hls/.run-id\"; "
        ": > \"$HOME/kodigui/drm-download.log\"; "
        ": > \"$HOME/kodigui/drm-mux.log\"; "
        // Пишем скрипт сборщика в файл (без проблем с кавычками).
        "cat > \"$HOME/kodigui/drm-assemble.sh\" << 'ASSEMBLE_EOF'\n" +
        assembleScriptBody +
        "ASSEMBLE_EOF\n"
        "chmod +x \"$HOME/kodigui/drm-assemble.sh\"; "
        // Загрузчик: расшифрованные сегменты в $HOME/kodigui/drm (без live-pipe-mux).
        "nohup N_m3u8DL-RE " + shellQuote(input) +
        " --key " + shellQuote(keydrm) +
        " -sv worst "
        "--save-name drm --save-dir \"$HOME/kodigui/drm\" "
        "--tmp-dir \"$HOME/kodigui/drm/tmp\" "
        "--mp4-real-time-decryption "
        "--live-real-time-merge "
        "--live-keep-segments "
        "--select-audio id='audio_aar=128000' "
        "> \"$HOME/kodigui/drm-download.log\" 2>&1 < /dev/null & "
        "echo $! > \"$HOME/kodigui/drm_download.pid\"; "
        // Публикатор динамических HLS-плейлистов → drm/hls/master.m3u8
        "nohup setsid \"$HOME/kodigui/drm-assemble.sh\" "
        "> \"$HOME/kodigui/drm-mux.log\" 2>&1 < /dev/null & "
        "echo $! > \"$HOME/kodigui/drm_mux.pid\"";

    runCommand("ssh", {user, remoteCmd});

    // Проверяем появление непустого drm.mp4 и состояние группы сборщика.
    if (drmFileCheckTimer)
        drmFileCheckTimer->stop();
    if (drmFileCheckProcess && drmFileCheckProcess->state() != QProcess::NotRunning)
        drmFileCheckProcess->kill();

    if (!drmFileCheckProcess) {
        drmFileCheckProcess = new QProcess(this);
        connect(drmFileCheckProcess,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            const QString output =
                QString::fromUtf8(drmFileCheckProcess->readAllStandardOutput());
            if (!output.isEmpty())
                qInfo().noquote() << "[DRM remote check]" << output.trimmed();
            qInfo() << "DRM remote check finished; exitCode=" << exitCode;

            if (exitCode == 0 && output.contains("DRM_HLS_READY")) {
                if (drmFileCheckTimer)
                    drmFileCheckTimer->stop();
                QMessageBox::information(this, "DRM",
                    "Новый динамический HLS-плейлист опубликован.\n"
                    "Нажмите Play — Kodi откроет поток один раз\n"
                    "и будет получать новые сегменты по мере загрузки.");
            } else if (output.contains("DRM_MUX_FAILED")) {
                if (drmFileCheckTimer)
                    drmFileCheckTimer->stop();
                QMessageBox::warning(this, "DRM",
                    "Публикатор HLS завершился до создания плейлиста.\n\n"
                    "Последние строки журнала:\n" + output);
            }
        });
    }

    if (!drmFileCheckTimer) {
        drmFileCheckTimer = new QTimer(this);
        drmFileCheckTimer->setInterval(1500);
        connect(drmFileCheckTimer, &QTimer::timeout, this, [this, runId, shellQuote]() {
            if (drmFileCheckProcess->state() != QProcess::NotRunning)
                return;

            // Ограничиваем ожидание, чтобы при ошибке SSH/загрузчика не проверять бесконечно.
            if (++drmFileCheckAttempts > 80) {
                drmFileCheckTimer->stop();
                QMessageBox::warning(this, "DRM",
                    "За ~2 минуты HLS-плейлист не появился.\n"
                    "Проверьте $HOME/kodigui/drm-download.log и $HOME/kodigui/drm-mux.log на Raspberry Pi.");
                return;
            }

            {
                qInfo() << "Checking remote DRM output and assembler status";
                drmFileCheckProcess->start("ssh", {
                    "pi@192.168.8.45",
                    "if [ -s \"$HOME/kodigui/drm/hls/.run-id\" ] && "
                    "   [ \"$(cat \"$HOME/kodigui/drm/hls/.run-id\" 2>/dev/null)\" = " + shellQuote(runId) + " ]; then "
                    "  if [ -s \"$HOME/kodigui/drm/hls/master.m3u8\" ] && "
                    "     [ -s \"$HOME/kodigui/drm/hls/video.m3u8\" ] && "
                    "     [ -s \"$HOME/kodigui/drm/hls/audio.m3u8\" ] && "
                    "     grep -q \"#EXTINF:\" \"$HOME/kodigui/drm/hls/video.m3u8\" && "
                    "     grep -q \"#EXTINF:\" \"$HOME/kodigui/drm/hls/audio.m3u8\"; then "
                    "    echo DRM_HLS_READY; "
                    "  elif [ -s \"$HOME/kodigui/drm_mux.pid\" ]; then "
                    "    p=$(cat \"$HOME/kodigui/drm_mux.pid\"); "
                    "    if ! kill -0 -- -\"$p\" 2>/dev/null; then "
                    "      echo DRM_MUX_FAILED; "
                    "      tail -n 25 $HOME/kodigui/drm-mux.log; "
                    "    fi; "
                    "  fi; "
                    "fi"
                });
            }
        });
    }
    drmFileCheckAttempts = 0;
    drmFileCheckTimer->start();

    qDebug() << "DRM: N_m3u8DL-RE + dynamic HLS publisher started";
}


QString MainWindow::currentDrmUrl()
{
    QString input = ui->lineEdit_drm->text();
    return input;
}


QString MainWindow::currentDrmKey()
{
    QString input = ui->lineEdit_drm_key->text();
    return input;
}


void MainWindow::on_stopStreamlinkButton_clicked()
{
    sshKillProcess("streamlink");
}

void MainWindow::on_killMatchTvStreamlinkButton_clicked()
{
    sshKillProcess("streamlink");
}

void MainWindow::on_closeMatchTvPopupButton_clicked()
{
    postInputAction("back");
}


void MainWindow::on_clearDrmFieldsButton_clicked()
{
    clearLineEditField(ui->lineEdit_drm, "Введите URL...");
    clearLineEditField(ui->lineEdit_drm_key, "Введите key...");
}


void MainWindow::on_playDrmHlsButton_clicked()
{
    postPlayerOpenFile("kodigui/drm/hls/master.m3u8");
}


void MainWindow::on_stopDrmDownloadButton_clicked()
{
    if (drmFileCheckTimer)
        drmFileCheckTimer->stop();
    if (drmFileCheckProcess && drmFileCheckProcess->state() != QProcess::NotRunning)
        drmFileCheckProcess->kill();

    // Остановка загрузчика и сборщика сегментов на удалённом устройстве.
    runCommand("ssh", {"pi@192.168.8.45",
        "if [ -s \"$HOME/kodigui/drm_mux.pid\" ]; then "
        "  p=$(cat \"$HOME/kodigui/drm_mux.pid\"); "
        "  kill -TERM -- -\"$p\" 2>/dev/null || true; "
        "fi; "
        "if [ -s \"$HOME/kodigui/drm_download.pid\" ]; then "
        "  p=$(cat \"$HOME/kodigui/drm_download.pid\"); "
        "  kill -TERM \"$p\" 2>/dev/null || true; "
        "fi; "
        "killall -9 N_m3u8DL-RE 2>/dev/null || true; "
        "rm -f \"$HOME/kodigui/drm_mux.pid\" \"$HOME/kodigui/drm_download.pid\" "
        "      \"$HOME/kodigui/drm-assemble.sh\""});
}


void MainWindow::on_closePopupButton_clicked()
{
    postInputAction("back");
}



void MainWindow::on_refreshSystemMonitorButton_clicked()
{
    QProcess *process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [=]() {
        QByteArray data = process->readAllStandardOutput();
        if (!data.isEmpty())
            qInfo().noquote() << "[Monitor stdout]" << QString::fromUtf8(data).trimmed();
        ui->textEdit->append(QString::fromUtf8(data));
    });

    connect(process, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray data = process->readAllStandardError();
        if (!data.isEmpty())
            qWarning().noquote() << "[Monitor stderr]" << QString::fromUtf8(data).trimmed();
        ui->textEdit->append(QString::fromUtf8(data));
    });

    QString program = "sshpass";
    QStringList arguments;

    arguments << "-p" << "639639"
              << "ssh"
              << "pi@192.168.8.45"
              << "top -b -n 1";
    ui->textEdit->clear();
    process->start(program, arguments);
}

