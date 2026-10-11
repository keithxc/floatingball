#include "screenshotpanel.h"
#include "screenrecorder.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QTimer>
#include <QSettings>
#include <QScreen>
#include <QComboBox>
#include <QSpinBox>
#include <QSet>
#include <QInputDialog>
#include <cstdio>
#include <algorithm>

// Native acceptance runner, invoked explicitly against a private output folder.
// The desktop portal's source chooser remains user-controlled on Wayland.
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc!=3 || !QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])))return 2;
    const QString root=QString::fromLocal8Bit(argv[1]);QDir().mkpath(root);
    QSet<QString> existingShots;for(const auto &file:QDir(root+"/shots").entryInfoList({"Screenshot-*.png"},QDir::Files))existingShots.insert(file.absoluteFilePath());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
    QLabel fixture(QStringLiteral("FloatingBall capture verification\nBLUE / GREEN / moving timestamp"));fixture.setWindowTitle("FloatingBall capture fixture");
    fixture.setStyleSheet("background:#248bd1;color:#ffffff;font-size:24px;padding:24px;");fixture.resize(700,350);fixture.move(200,180);fixture.show();
    QTimer motion;motion.setInterval(250);int ticks=0;QObject::connect(&motion,&QTimer::timeout,[&]{fixture.setText(QStringLiteral("FloatingBall capture verification\nFrame %1").arg(++ticks));});motion.start();
    const QString action=QString::fromLocal8Bit(argv[2]);int result=1;
    QTimer::singleShot(90000,&app,[&]{fprintf(stderr,"FAIL: native capture acceptance timed out\n");app.quit();});
    ScreenshotPanel panel(nullptr,root+"/shots");ScreenRecorder recorder;
    if(action=="record" || action=="record-window") {
        QTimer picker;picker.setInterval(100);
        if(action=="record-window") {
            QObject::connect(&picker,&QTimer::timeout,[&]{
                if(auto dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))if(dialog->windowTitle()==QStringLiteral("选择录制窗口")) {
                    const auto combo=dialog->findChild<QComboBox *>();if(!combo)return;
                    for(int i=0;i<combo->count();++i)if(combo->itemText(i).contains(QStringLiteral("FloatingBall capture fixture"))){combo->setCurrentIndex(i);dialog->accept();picker.stop();return;}
                }
            });picker.start();
        }
        QObject::connect(&recorder,&ScreenRecorder::started,[&]{printf("RECORDING_STARTED\n");fflush(stdout);QTimer::singleShot(3500,&recorder,&ScreenRecorder::stop);});
        QObject::connect(&recorder,&ScreenRecorder::finished,[&](const QString &file,const QString &error){
            if(!file.isEmpty()&&QFileInfo(file).size()>1000){printf("PASS: native recording saved %s (%lld bytes)\n",file.toUtf8().constData(),QFileInfo(file).size());result=0;}
            else fprintf(stderr,"FAIL: recording: %s\n",error.toUtf8().constData());fflush(stdout);app.quit();
        });
        QTimer::singleShot(500,&app,[&]{recorder.start(root,fixture.geometry().center(),action=="record-window");});
        app.exec();recorder.stop();return result;
    } else {
        panel.reveal(QPoint(1100,300));CaptureOptions options;
        options.mode=action=="region"?CaptureOptions::Region:action=="window"?CaptureOptions::Window:action=="screen"?CaptureOptions::CurrentScreen:CaptureOptions::AllScreens;
        QTimer::singleShot(600,&app,[&]{panel.takeScreenshot(fixture.geometry().center(),options);});
        QTimer *poll=new QTimer(&app);poll->setInterval(150);
        QObject::connect(poll,&QTimer::timeout,[&]{
            auto files=QDir(root+"/shots").entryInfoList({"Screenshot-*.png"},QDir::Files);
            files.erase(std::remove_if(files.begin(),files.end(),[&](const QFileInfo &file){return existingShots.contains(file.absoluteFilePath());}),files.end());
            if(files.isEmpty())return;QImage image(files.first().absoluteFilePath());if(image.isNull())return;
            printf("PASS: native %s capture saved %dx%d PNG at %s\n",action.toUtf8().constData(),image.width(),image.height(),files.first().absoluteFilePath().toUtf8().constData());
            // Verify that a real known desktop surface reached the capture.
            bool marker=false;for(int y=0;y<image.height();y+=qMax(1,image.height()/100))for(int x=0;x<image.width();x+=qMax(1,image.width()/100)){
                const QColor color=image.pixelColor(x,y);if(qAbs(color.red()-36)<12&&qAbs(color.green()-139)<12&&qAbs(color.blue()-209)<12)marker=true;
            }
            if(!marker){fprintf(stderr,"FAIL: capture did not contain fixture pixels\n");app.quit();return;}
            printf("PASS: native capture contains fixture pixels\n");fflush(stdout);result=0;app.quit();
        });poll->start();
    }
    app.exec();recorder.stop();return result;
}
