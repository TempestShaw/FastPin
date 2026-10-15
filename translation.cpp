#include "translation.h"

#include <QRegularExpression>
#include <QResource>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocumentFragment>

static QString dictionaryKey(const QString &text)
{
    QString key = text.simplified().toCaseFolded();
    const QString punctuation = ".,!?;:()[]\"“”‘’。！？";
    while (!key.isEmpty() && punctuation.contains(key.front())) key.remove(0, 1);
    while (!key.isEmpty() && punctuation.contains(key.back())) key.chop(1);
    return key.trimmed();
}

QString offlineDefinition(const QString &text)
{
    // Referencing the resource also keeps it linked into static-library consumers.
    static const QByteArray data = [] {
        Q_INIT_RESOURCE(dictionary);
        return qUncompress(QResource(":/dictionary/cedict.tsv.qz").uncompressedData());
    }();
    const QByteArray key = dictionaryKey(text).toUtf8();
    if (key.isEmpty()) return {};
    qsizetype low = 0, high = data.size();
    while (low < high) {
        const qsizetype middle = low + (high - low) / 2;
        const qsizetype start = middle == 0 ? 0 : data.lastIndexOf('\n', middle - 1) + 1;
        const qsizetype end = data.indexOf('\n', middle);
        const qsizetype tab = data.indexOf('\t', start);
        const int order = QByteArrayView(data.constData() + start, tab - start).compare(key);
        if (order < 0) low = end + 1;
        else if (order > 0) high = start;
        else {
            static const QRegularExpression pronunciation("\\s*\\[[^\\]]*\\]");
            static const QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");
            const bool chineseEntry = QString::fromUtf8(key).contains(chinese);
            auto lines = QString::fromUtf8(data.constData() + tab + 1, end - tab - 1).split("\\n");
            for (auto &line : lines) {
                line.remove(pronunciation);
                if (!chineseEntry) line = line.section(QStringLiteral(" — "), 0, 0).trimmed();
            }
            lines.removeDuplicates();
            return lines.join(chineseEntry ? QStringLiteral("\n") : QStringLiteral("，"));
        }
    }
    return {};
}

bool isDictionaryWord(const QString &text)
{
    static const QRegularExpression word("^[\\p{L}\\p{M}]+(?:['’-][\\p{L}\\p{M}]+)*$");
    static const QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");
    const auto key = dictionaryKey(text);
    return key.size() <= (key.contains(chinese) ? 4 : 64) && word.match(key).hasMatch();
}

QUrl onlineTranslationUrl(const QString &text)
{
    static const QRegularExpression chinese("[\\x{3400}-\\x{9fff}]");
    QUrl url("https://api.mymemory.translated.net/get");
    QUrlQuery query;
    query.addQueryItem("langpair", text.contains(chinese) ? "zh-TW|en" : "en|zh-TW");
    query.addQueryItem("q", text);
    url.setQuery(query);
    return url;
}

TranslationResult parseTranslationReply(const QByteArray &json, bool transportFailed)
{
    const auto root = QJsonDocument::fromJson(json).object();
    if (root.value("quotaFinished").toBool() || root.value("responseStatus").toVariant().toInt() == 429)
        return {{}, "The free online translation quota is used up. Try again tomorrow."};
    if (transportFailed) return {{}, "Online translation unavailable. Check your connection and try again."};
    const auto text = root.value("responseData").toObject().value("translatedText").toString();
    if (root.value("responseStatus").toVariant().toInt() != 200 || text.trimmed().isEmpty())
        return {{}, "Online translation unavailable. Try again later."};
    return {QTextDocumentFragment::fromHtml(text).toPlainText(), {}};
}
