#include "floatingwindow.h"
#include "panelbackdrop.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <unistd.h>
#import <AppKit/AppKit.h>

class BackdropPattern final : public QWidget {
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const QColor colors[]{QColor("#6baee8"),QColor("#d58abe"),QColor("#66bca3"),QColor("#e3ad65")};
        for(int y=0;y<height();y+=8)for(int x=0;x<width();x+=8)
            p.fillRect(x,y,8,8,colors[(x/150)%4].lighter((x/8+y/8)%2?125:75));
        p.setPen(QColor("#193247"));p.setFont(QFont("Arial",18,QFont::Bold));
        for(int y=45;y<height();y+=60)p.drawText(20,y,"BACKGROUND DETAIL 7391");
    }
};

static NSView *effectFor(QWidget *widget,int &count,bool &isGlass){
    NSView *view=reinterpret_cast<NSView *>(widget->winId());NSView *effect=nil;count=0;isGlass=false;
    Class glassCls=NSClassFromString(@"FloatingPanelGlassEffectView");
    Class vfxCls=NSClassFromString(@"FloatingPanelVisualEffectView");
    for(NSView *child in view.superview.subviews){
        if(glassCls&&[child isKindOfClass:glassCls]){effect=child;isGlass=true;++count;}
        else if(vfxCls&&[child isKindOfClass:vfxCls]){effect=child;isGlass=false;++count;}
    }
    return effect;
}

int main(int argc,char **argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc!=2||!QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])))return 2;
    QDir().mkpath(QString::fromLocal8Bit(argv[1]));QTemporaryDir data(QString::fromLocal8Bit(argv[1])+"/glass-XXXXXX");if(!data.isValid())return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
    printf("OWNED_PID=%d auto_exit_seconds=20 synthetic_data=%s\n",getpid(),data.path().toUtf8().constData());fflush(stdout);
    if(!PanelBackdrop::supported()){printf("SKIP native backdrop requires Cocoa\n");return 77;}
    BackdropPattern pattern;pattern.setWindowTitle("Owned backdrop pattern 7391");pattern.resize(700,460);pattern.move(240,180);pattern.show();
    QWidget glass;glass.setObjectName("glassCheck");glass.setWindowTitle("FloatingBall Glass Check");glass.setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);glass.setAttribute(Qt::WA_TranslucentBackground);glass.setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    FloatingWindow::setPanelStyleSheet(&glass,"QWidget#glassCheck{background:@panelTint@;border:1px solid rgba(255,255,255,35);border-radius:18px;} QLabel{color:#f1f5fa;font:18px Arial;} QLineEdit{color:#f1f5fa;background:rgba(0,0,0,125);padding:8px;border:1px solid rgba(255,255,255,45);border-radius:8px;}");
    auto layout=new QVBoxLayout(&glass);layout->setContentsMargins(20,20,20,20);layout->addWidget(new QLabel("Sharp foreground · 7391"));layout->addWidget(new QLineEdit("Text stays clear"));layout->addStretch();
    glass.resize(380,230);glass.move(330,260);FloatingWindow::prepare(&glass,true);glass.show();glass.raise();glass.activateWindow();
    NSView *qtView=reinterpret_cast<NSView *>(glass.winId());NSView *originalContent=qtView.window.contentView;
    bool passed=false;int phase=0;QString tintBefore;
    QTimer checks;checks.setInterval(300);
    QObject::connect(&checks,&QTimer::timeout,&glass,[&]{
        if(glass.property("panelBackdropReduceTransparency").toBool()){printf("SKIP system Reduce Transparency is enabled; unchanged\n");app.exit(77);return;}
        int count=0;bool isGlass=false;auto effect=effectFor(&glass,count,isGlass);qtView=reinterpret_cast<NSView *>(glass.winId());
        const bool expectActive=phase!=1;
        if(qtView.window.contentView!=originalContent||glass.property("panelBackdropBlurActive").toBool()!=expectActive||count!=(expectActive?1:0)){
            fprintf(stderr,"FAIL native lifecycle phase=%d count=%d active=%d status=%s\n",phase,count,int(glass.property("panelBackdropBlurActive").toBool()),glass.property("panelBackdropBlurStatus").toString().toUtf8().constData());app.exit(1);return;
        }
        if(glass.windowOpacity()!=1.0||glass.styleSheet().contains("@panelTint@")){fprintf(stderr,"FAIL tint template must preserve full foreground opacity\n");app.exit(1);return;}
        if(expectActive&&(!NSEqualRects(effect.frame,qtView.frame)||[qtView.superview.subviews indexOfObject:effect]>=[qtView.superview.subviews indexOfObject:qtView]||[effect hitTest:NSMakePoint(12,12)]!=nil)){
            fprintf(stderr,"FAIL native effect is not a decorative lower sibling\n");app.exit(1);return;
        }
        if(expectActive&&!isGlass){
            auto vfx=static_cast<NSVisualEffectView *>(effect);
            if(vfx.blendingMode!=NSVisualEffectBlendingModeBehindWindow||vfx.state!=NSVisualEffectStateActive||vfx.material!=NSVisualEffectMaterialUnderWindowBackground||![vfx.appearance.name isEqualToString:NSAppearanceNameDarkAqua]||vfx.isEmphasized||vfx.alphaValue!=1.0){
                fprintf(stderr,"FAIL native backdrop must use dark under-window material without extra emphasis or whole-view fading\n");app.exit(1);return;
            }
        }
        if(phase==4&&(glass.styleSheet()==tintBefore||FloatingWindow::panelOpacity()!=0.55)){fprintf(stderr,"FAIL panel tint change did not update CSS independently of window opacity\n");app.exit(1);return;}
        printf("NATIVE_BACKDROP phase=%d active=%d count=%d liquid_glass=%d qt_content_unchanged=1\n",phase,int(expectActive),count,int(isGlass));fflush(stdout);
        if(expectActive){printf("NATIVE_DARK_GLASS kind=%s appearance=%s native_view_opaque=%d window_opacity=%.2f tint=%.2f\n",isGlass?"NSGlassEffectView":"NSVisualEffectView",effect.appearance.name.UTF8String,int([effect isOpaque]),glass.windowOpacity(),FloatingWindow::panelOpacity());fflush(stdout);}
        if(phase==0)FloatingWindow::setPanelBlurEnabled(false);
        else if(phase==1)FloatingWindow::setPanelBlurEnabled(true);
        else if(phase==2)glass.resize(420,250);
        else if(phase==3){tintBefore=glass.styleSheet();FloatingWindow::setPanelOpacity(0.55);}
        else {passed=true;checks.stop();printf("PASS dark material/appearance, enable/disable/re-enable/resize and black tint CSS updates with window opacity fixed at 1. Holding synthetic scene for visual inspection; AppKit controls blur strength and material tint.\n");fflush(stdout);}
        ++phase;
    });checks.start();
    QTimer::singleShot(20000,&app,[&]{if(passed)app.quit();else app.exit(3);});
    return app.exec();
}
