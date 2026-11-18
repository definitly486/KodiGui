// m3u8interceptor.cpp
#include "m3u8interceptor.h"
#include <QDebug>

M3u8Interceptor::M3u8Interceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

void M3u8Interceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QString url = info.requestUrl().toString();
  if (url.contains(".m3u8") && url.startsWith("http")) {
    qDebug().noquote() << "INTERCEPTOR → УНИВЕРСАЛЬНЫЙ ПОТОК:" << url;
    emit streamFound(url);
    return; // один раз — и хватит
}
}