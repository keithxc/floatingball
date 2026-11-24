#include "screentext.h"
#include <QBuffer>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <thread>
#include <chrono>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <ImageIO/ImageIO.h>

namespace ScreenText {
Result recognizeNative(const QImage &image) {
    @autoreleasepool {
        QByteArray png;
        QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return {{}, QStringLiteral("无法准备识别图片，请重试。"), QStringLiteral("Apple Vision")};
        NSData *data = [NSData dataWithBytes:png.constData() length:png.size()];
        CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
        CGImageRef pixels = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
        if (source) CFRelease(source);
        if (!pixels)
            return {{}, QStringLiteral("无法读取识别图片，请重试。"), QStringLiteral("Apple Vision")};

        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = YES;
        NSError *error = nil;
        NSArray<NSString *> *supported = [request supportedRecognitionLanguagesAndReturnError:&error];
        NSMutableArray<NSString *> *preferred = [NSMutableArray array];
        for (NSString *language in @[@"zh-Hans", @"zh-Hant", @"en-US"])
            if ([supported containsObject:language]) [preferred addObject:language];
        if (preferred.count) request.recognitionLanguages = preferred;
        if (@available(macOS 13.0, *)) request.automaticallyDetectsLanguage = YES;
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:pixels options:@{}];
        // Vision performs synchronously on this worker. Cancellation must reach
        // the native request from another thread while performRequests is busy.
        std::atomic<bool> completed(false);
        QThread *worker=QThread::currentThread();
        std::thread cancellation([request,worker,&completed]{
            @autoreleasepool {
                while(!completed.load()) {
                    if(worker->isInterruptionRequested()){[request cancel];return;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
            }
        });
        const BOOL success = [handler performRequests:@[request] error:&error];
        completed.store(true);cancellation.join();
        CGImageRelease(pixels);
        if (!success) {
            const QString detail = error ? QString::fromUtf8(error.localizedDescription.UTF8String) : QStringLiteral("未知错误");
            [handler release]; [request release];
            return {{}, worker->isInterruptionRequested()?QStringLiteral("文字识别已取消"):QStringLiteral("本地文字识别失败：") + detail, QStringLiteral("Apple Vision")};
        }
        QStringList lines;
        for (VNRecognizedTextObservation *observation in request.results) {
            VNRecognizedText *candidate = [[observation topCandidates:1] firstObject];
            if (candidate.string.length) lines.append(QString::fromUtf8(candidate.string.UTF8String));
        }
        [handler release]; [request release];
        if(QThread::currentThread()->isInterruptionRequested())return {{},QStringLiteral("文字识别已取消"),QStringLiteral("Apple Vision")};
        const QString text = lines.join('\n').trimmed();
        return {text, text.isEmpty() ? QStringLiteral("没有识别到文字。可尝试截取更清晰的区域。") : QString(), QStringLiteral("Apple Vision · 本地识别")};
    }
}
}
