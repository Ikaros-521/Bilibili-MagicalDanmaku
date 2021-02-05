#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QStyle>
#include <QtConcurrent/QtConcurrent>
#include "livevideoplayer.h"
#include "ui_livevideoplayer.h"
#include "facilemenu.h"

LiveVideoPlayer::LiveVideoPlayer(QSettings &settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LiveVideoPlayer),
    settings(settings)
{
    ui->setupUi(this);
    setModal(false);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    int uw = qMax(style()->pixelMetric(QStyle::PM_TitleBarHeight), 32);
    ui->playButton->setFixedSize(uw, uw);
    ui->saveCapture1Button->setFixedSize(uw, uw);
    ui->saveCapture5sButton->setFixedSize(uw, uw);
    ui->saveCapture13sButton->setFixedSize(uw, uw);
    ui->saveCapture30sButton->setFixedSize(uw, uw);
    ui->saveCapture60sButton->setFixedSize(uw, uw);

    player = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    connect(player, &QMediaPlayer::positionChanged, this, [=](qint64 position){
        // 直播的话，一般都是 position=0, duration=0
        // qDebug() << "position changed:" << position << player->duration();
    });
    player->setVideoOutput(ui->videoWidget);

    connect(player, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state) {
        if (state == QMediaPlayer::PlayingState)
        {
            ui->playButton->setIcon(QIcon(":/icons/pause"));
            calcVideoRect();
            if (enablePrevCapture)
                startCapture();
        }
        else if (state == QMediaPlayer::PausedState)
        {
            ui->playButton->setIcon(QIcon(":/icons/play"));
            if (enablePrevCapture)
                stopCapture(false);
        }
        // qDebug() << "videoPlayer.stateChanged:" << state << QDateTime::currentDateTime();
    });

    // 设置音量
    player->setVolume(settings.value("videoplayer/volume", 50).toInt());

    // 设置全屏
    if (settings.value("videoplayer/fullScreen", false).toBool())
        ui->videoWidget->setFullScreen(true);

    // 设置预先截图
    captureTimer = new QTimer(this);
    captureTimer->setInterval(100);
    connect(captureTimer, SIGNAL(timeout()), this, SLOT(slotSaveCurrentCapture()));
    if (!(enablePrevCapture = settings.value("videoplayer/capture", false).toBool()))
        showCaptureButtons(false);
    captureDir = QDir(QApplication::applicationDirPath() + "/capture");

    probe = new QVideoProbe(this);
    connect(probe, SIGNAL(videoFrameProbed(const QVideoFrame &)), this, SLOT(processFrame(const QVideoFrame &)));
    probe->setSource(player);
}

LiveVideoPlayer::~LiveVideoPlayer()
{
    delete ui;
}

void LiveVideoPlayer::setRoomId(QString roomId)
{
    this->roomId = roomId;
    refreshPlayUrl();

    captureDir = QDir(QApplication::applicationDirPath() + "/capture/" + roomId);
}

void LiveVideoPlayer::slotLiveStart(QString roomId)
{
    if (this->roomId != roomId)
        return ;
    QTimer::singleShot(500, [=]{
        refreshPlayUrl();
    });
}

void LiveVideoPlayer::setPlayUrl(QString url)
{
    player->setMedia(QMediaContent((QUrl(url))));
    if (player->state() == QMediaPlayer::StoppedState)
        player->play();
}

void LiveVideoPlayer::refreshPlayUrl()
{
    if (roomId.isEmpty())
        return ;
    QString url = "http://api.live.bilibili.com/room/v1/Room/playUrl?cid=" + roomId
            + "&quality=4&qn=10000&platform=web&otype=json";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray data = reply->readAll();
        manager->deleteLater();
        delete request;
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qDebug() << ("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取链接
        QJsonArray array = json.value("data").toObject().value("durl").toArray();
        if (!array.size())
        {
            QMessageBox::warning(this, "播放视频", "未找到可用的链接\n" + data);
            return ;
        }
        QString url = array.first().toObject().value("url").toString(); // 第一个链接
        qDebug() << "playUrl:" << url;
        setPlayUrl(url);
    });
    manager->get(*request);
}

void LiveVideoPlayer::processFrame(const QVideoFrame &frame)
{
    qDebug() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~";
    qDebug() << frame.size();
}

void LiveVideoPlayer::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    restoreGeometry(settings.value("videoplayer/geometry").toByteArray());

    if (!player->media().isNull())
    {
        player->play();
    }
}

void LiveVideoPlayer::hideEvent(QHideEvent *e)
{
    QDialog::hideEvent(e);
    settings.setValue("videoplayer/geometry", this->saveGeometry());

    if (player->state() == QMediaPlayer::PlayingState)
    {
        player->pause();
    }
}

void LiveVideoPlayer::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    calcVideoRect();
}

void LiveVideoPlayer::on_videoWidget_customContextMenuRequested(const QPoint&)
{
    newFacileMenu;
    menu->addAction(QIcon(":/icons/play"), "播放", [=]{
        player->play();
    })->hide(player->state() != QMediaPlayer::PlayingState);
    menu->addAction(QIcon(":/icons/pause"), "暂停", [=]{
        player->pause();
    })->hide(player->state() == QMediaPlayer::PlayingState);

    menu->addAction(QIcon(":icons/list_circle"), "刷新", [=]{
        refreshPlayUrl();
    });

    menu->split()->addAction(QIcon(), "全屏", [=]{
        bool fullScreen = !ui->videoWidget->isFullScreen();
        ui->videoWidget->setFullScreen(fullScreen);
        settings.setValue("videoplayer/fullScreen", fullScreen);
    })->check(ui->videoWidget->isFullScreen());

    menu->addAction(QIcon(":/icons/favorite"), "缩放", [=]{
        QSize size = ui->videoWidget->sizeHint();
        if (size.height() < 10 || size.width() < 10)
            return ;
        QSize minSize = ui->videoWidget->minimumSize();
        ui->videoWidget->setFixedSize(size);
        this->layout()->activate();
        this->adjustSize();
        ui->videoWidget->setMinimumSize(minSize);
    });

    FacileMenu* volumeMenu = menu->split()->addMenu(QIcon(":/icons/volume"), "音量");
    volumeMenu->addNumberedActions("%1", 0, 110, [=](FacileMenuItem* item, int val){
        item->check(val == (player->volume()+5) / 10 * 10);
    }, [=](int vol){
        settings.setValue("videoplayer/volume", vol);
        player->setVolume(vol);
    }, 10);

    menu->addAction(QIcon(":/icons/mute"), "静音", [=]{
        if (player->volume())
            player->setVolume(0);
        else
            player->setVolume(settings.value("videoplayer/volume", 50).toInt());
    })->check(!player->volume());

    menu->split()->addAction(QIcon(":/icons/history"), "截图", [=]{
        enablePrevCapture = !settings.value("videoplayer/capture", false).toBool();
        settings.setValue("videoplayer/capture", enablePrevCapture);
        if (enablePrevCapture)
        {
            if (player->state() == QMediaPlayer::PlayingState)
                startCapture();
        }
        else
        {
            stopCapture(true);
        }
        showCaptureButtons(enablePrevCapture);
    })->check(enablePrevCapture);

    menu->exec();
}

void LiveVideoPlayer::on_playButton_clicked()
{
    if (player->state() == QMediaPlayer::PlayingState)
        player->pause();
    else
        player->play();
}

void LiveVideoPlayer::on_saveCapture1Button_clicked()
{
    saveCapture();
}

void LiveVideoPlayer::on_saveCapture5sButton_clicked()
{
    saveCapture(5);
}

void LiveVideoPlayer::on_saveCapture13sButton_clicked()
{
    saveCapture(13);
}

void LiveVideoPlayer::on_saveCapture30sButton_clicked()
{
    saveCapture(30);
}

void LiveVideoPlayer::on_saveCapture60sButton_clicked()
{
    saveCapture(60);
}

/**
 * 位置改变时，计算视频的位置
 */
void LiveVideoPlayer::calcVideoRect()
{
    QSize hint = ui->videoWidget->sizeHint(); // 视频大小
    QSize size = ui->videoWidget->size();
    if (size.width() < 1 || size.height() < 1)
        return ;
    if (hint.width() < 8 || hint.height() < 8)
        hint = size;
    double hintProb = double(hint.width()) / hint.height();
    double sizeProb = double(size.width()) / size.height();
    if (hintProb >= sizeProb) // 上下黑
    {
        videoRect = QRect(0, (size.height() - size.width() / hintProb) / 2,
                          size.width(), size.width() / hintProb);
    }
    else // 左右黑
    {
        videoRect = QRect((size.width() - size.height() * hintProb) / 2, 0,
                          size.height() * hintProb, size.height());
    }
}

void LiveVideoPlayer::slotSaveCurrentCapture()
{
    if (!capturePixmaps)
    {
        qWarning() << "未初始化capturePixmaps";
        return ;
    }

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QPixmap* pixmap = new QPixmap(videoRect.size());
    ui->videoWidget->render(pixmap, QPoint(0, 0), videoRect);
    if (!pixmap->isNull())
        capturePixmaps->append(QPair<qint64, QPixmap*>(timestamp, pixmap));
    // qDebug() << "预先截图" << videoRect << timestamp << capturePixmaps->size();

    while (capturePixmaps->size())
    {
        if (capturePixmaps->first().first + captureMaxLong < timestamp)
            delete capturePixmaps->takeFirst().second;
        else
            break;
    }
}

void LiveVideoPlayer::startCapture()
{
    if (!capturePixmaps)
        capturePixmaps = new QList<QPair<qint64, QPixmap*>>();
    captureTimer->start();
}

void LiveVideoPlayer::stopCapture(bool clear)
{
    captureTimer->stop();
    if (clear && capturePixmaps)
    {
        foreach (auto pair, *capturePixmaps)
        {
            delete pair.second;
        }
        delete capturePixmaps;
        capturePixmaps = nullptr;
    }
}

void LiveVideoPlayer::saveCapture()
{
    if (!capturePixmaps)
    {
        qWarning() << "未初始化capturePixmaps";
        return ;
    }

    QPixmap* pixmap = new QPixmap(videoRect.size());
    ui->videoWidget->render(pixmap, QPoint(0, 0), videoRect);
    captureDir.mkpath(captureDir.absolutePath());
    pixmap->save(timeToPath(QDateTime::currentMSecsSinceEpoch()));
}

void LiveVideoPlayer::saveCapture(int second)
{
    if (!capturePixmaps)
    {
        qWarning() << "未初始化capturePixmaps";
        return ;
    }

    // 重新开始一轮新的
    auto list = this->capturePixmaps;
    this->capturePixmaps = nullptr;
    startCapture();

    // 子线程保存图片
    try {
        QtConcurrent::run([=]{
            int delta = second * 1000;
            qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();

            QDir dir = captureDir;
            QString dirName = timeToFileName(currentTimestamp);
            dir.mkpath(dir.absolutePath() + "/" + dirName);
            dir.cd(dirName);

            // 保存录制参数
            QSettings params(dir.absoluteFilePath(CAPTURE_PARAM_FILE), QSettings::IniFormat);
            params.setValue("gif/interval", captureTimer->interval());

            // 计算要保存的起始位置
            int maxSize = list->size();
            int start = maxSize;
            while (start > 0 && list->at(start-1).first + delta >= currentTimestamp)
                start--;

            // 清理无用的
            for (int i = 0; i < start; i++)
                delete list->at(i).second;

            // 确保有保存的项
            if (start >= maxSize)
                return ;

            // 记录保存时间
            params.setValue("time/start", list->at(start).first);
            params.setValue("time/end", list->at(maxSize-1).first);
            params.sync();

            // 开始保存
            for (int i = start; i < maxSize; i++)
            {
                auto cap = list->at(i);
                cap.second->save(dir.absoluteFilePath(timeToFileName(cap.first)), "jpg");
                qDebug() << "文件位置：" << dir.absoluteFilePath(timeToFileName(cap.first));
                delete cap.second;
            }
            qDebug() << "已保存" << (maxSize-start) << "张预先截图";
            delete list;
        });
    } catch (...) {
        qDebug() << "创建保存线程失败，请增加间隔（降低帧率）";
    }
}

void LiveVideoPlayer::showCaptureButtons(bool show)
{
    static QList<QWidget*> ws {
        ui->playButton,
                ui->saveCapture1Button,
                ui->saveCapture5sButton,
                ui->saveCapture13sButton,
                ui->saveCapture30sButton,
                ui->saveCapture60sButton,
                ui->widget
    };

    foreach (QWidget* w, ws)
    {
        w->setVisible(show);
    }
}

QString LiveVideoPlayer::timeToPath(const qint64 &time)
{
    return captureDir.filePath(timeToFileName(time) + ".jpg");
}

QString LiveVideoPlayer::timeToFileName(const qint64 &time)
{
    return QDateTime::fromMSecsSinceEpoch(time)
            .toString("yyyy-MM-dd hh-mm-ss.zzz");
}
