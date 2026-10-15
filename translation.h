#pragma once

#include <QString>
#include <QUrl>

QString offlineDefinition(const QString &text);
bool isDictionaryWord(const QString &text);
QUrl onlineTranslationUrl(const QString &text);
struct TranslationResult { QString text, error; };
TranslationResult parseTranslationReply(const QByteArray &json);
