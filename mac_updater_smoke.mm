// Exercise the production Sparkle delegate with a separate app identity and temporary data.
#include "settings_mac.mm"
#include <QApplication>
#include <QFile>
#include <QTimer>

static void record(NSString *message)
{
    QFile file(QString::fromUtf8([[NSBundle.mainBundle objectForInfoDictionaryKey:@"SmokeResultPath"] UTF8String]));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write([[message stringByAppendingString:@"\n"] UTF8String]);
}

@interface FastPinUpdateProbe : FastPinSettingsController
@end
@implementation FastPinUpdateProbe
- (BOOL)updater:(SPUUpdater *)updater willInstallUpdateOnQuit:(SUAppcastItem *)item immediateInstallationBlock:(void (^)(void))handler
{
    const BOOL result = [super updater:updater willInstallUpdateOnQuit:item immediateInstallationBlock:handler];
    record(@"verified download");
    QTimer::singleShot(100, [self] {
        if ([[NSBundle.mainBundle objectForInfoDictionaryKey:@"SmokeQuitNormally"] boolValue]) [self quit:nil];
        else [self checkForUpdates:nil];
    });
    return result;
}
- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
    [super updater:updater didAbortWithError:error];
    record([NSString stringWithFormat:@"rejected %ld", (long)error.code]);
    QCoreApplication::exit(0);
}
@end

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    NSString *version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"];
    record([@"launched " stringByAppendingString:version]);
    QSettings settings(QString::fromUtf8([[NSBundle.mainBundle objectForInfoDictionaryKey:@"SmokeSettingsPath"] UTF8String]), QSettings::IniFormat);
    if ([version isEqualToString:@"2"]) {
        record(settings.value("shortcuts/0").toString() == "Meta+S" ? @"settings preserved" : @"settings lost");
        return 0;
    }
    settings.setValue("shortcuts/0", "Meta+S"); settings.sync();
    FastPinUpdateProbe *probe = [[FastPinUpdateProbe alloc] init];
    [probe startUpdater];
    QTimer::singleShot(100, [probe] { [probe->updates.updater checkForUpdatesInBackground]; });
    QTimer::singleShot(45000, [] { record(@"timeout"); QCoreApplication::exit(2); });
    return app.exec();
}
