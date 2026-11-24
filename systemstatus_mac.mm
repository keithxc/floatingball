#include "systemstatus.h"

#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <IOKit/ps/IOPSKeys.h>
#import <IOKit/ps/IOPowerSources.h>

namespace SystemStatusMac {

void populateSnapshot(SystemStatusSnapshot *out)
{
    if (!out) return;
    @autoreleasepool {
        // 1. Battery via IOKit Power Sources (present on MacBooks, absent on Mac mini)
        CFTypeRef blob = IOPSCopyPowerSourcesInfo();
        if (blob) {
            CFArrayRef sources = IOPSCopyPowerSourcesList(blob);
            bool foundBattery = false;
            if (sources) {
                const CFIndex count = CFArrayGetCount(sources);
                for (CFIndex i = 0; i < count; ++i) {
                    CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, i));
                    if (!desc) continue;
                    CFStringRef type = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSTypeKey));
                    if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType))) continue;
                    foundBattery = true;
                    int cur = 0, maxCap = 100;
                    if (CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey)))
                        CFNumberGetValue(n, kCFNumberIntType, &cur);
                    if (CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey)))
                        CFNumberGetValue(n, kCFNumberIntType, &maxCap);
                    out->batteryPresent = true;
                    out->batteryPercent = maxCap > 0 ? qBound(0, qRound(cur * 100.0 / maxCap), 100) : 0;
                    CFStringRef psState = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSPowerSourceStateKey));
                    out->batteryPluggedIn = (psState && CFEqual(psState, CFSTR(kIOPSACPowerValue)));
                    break;
                }
                CFRelease(sources);
            }
            CFRelease(blob);
            if (!foundBattery) out->batteryPresent = false;
        }

        // 2. Current keyboard input source ("双" vs "EN")
        TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
        if (current) {
            NSString *srcId = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyInputSourceID);
            NSString *name = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyLocalizedName);
            const QString qId = srcId ? QString::fromNSString(srcId) : QString();
            const QString qName = name ? QString::fromNSString(name) : QString();
            const bool isAscii = qId.contains(QLatin1String("ABC"), Qt::CaseInsensitive) ||
                                 qId.contains(QLatin1String("US"), Qt::CaseInsensitive) ||
                                 qId.contains(QLatin1String("British"), Qt::CaseInsensitive);
            out->inputPresent = !qId.isEmpty();
            out->inputLabel = isAscii ? QStringLiteral("EN") : QStringLiteral("双");
            out->inputDescription = qName.isEmpty() ? out->inputLabel : qName;
            CFRelease(current);
        }
    }
}

} // namespace SystemStatusMac
