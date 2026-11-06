#pragma once

#include <QString>
#include <QUrl>
#include <QList>

bool containsHan(const QString &text);
QString offlineDefinition(const QString &text);
bool isDictionaryWord(const QString &text);
QUrl onlineTranslationUrl(const QString &text);
QList<QUrl> browserTranslationUrls(const QString &text);
struct TranslationResult { QString text, error; };
// transportFailed: the request never got an HTTP reply (offline, DNS, timeout).
TranslationResult parseTranslationReply(const QByteArray &json, bool transportFailed = false);
