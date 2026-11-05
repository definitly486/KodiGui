// m3u8interceptor.cpp
#include "m3u8interceptor.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

M3u8Interceptor::M3u8Interceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
    m_lastLicenseReq.isValid = false;
}

void M3u8Interceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QString url = info.requestUrl().toString();
    const QString method = info.requestMethod();

    // 1. Ловим манифесты (MPD для DASH, M3U8 для HLS)
    if ((url.contains(".m3u8") || url.contains(".mpd")) && url.startsWith("http")) {
        qDebug().noquote() << "INTERCEPTOR → MANIFEST FOUND:" << url;
        emit streamFound(url);
        return;
    }

    // 2. Ловим POST-запросы к лицензионным серверам Widevine
    if (method == "POST" && (url.contains("license") || url.contains("widevine") || url.contains("drm") || url.contains("cenc"))) {
        qDebug().noquote() << "INTERCEPTOR → LICENSE REQUEST CAUGHT:" << url;
        
        m_lastLicenseReq.url = url;
        m_lastLicenseReq.headers = convertHeaders(info.requestHeaders());
        m_lastLicenseReq.isValid = true;
    }
}

QString M3u8Interceptor::convertHeaders(const QList<QWebEngineHttpRequestHeader> &headers) const
{
    QJsonObject json;
    for (const auto &header : headers) {
        json[QString::fromUtf8(header.name())] = QString::fromUtf8(header.value());
    }
    return QString(QJsonDocument(json).toJson(QJsonDocument::Compact));
}