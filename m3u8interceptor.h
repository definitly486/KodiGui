// m3u8interceptor.h
#ifndef M3U8INTERCEPTOR_H
#define M3U8INTERCEPTOR_H

#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>

class M3u8Interceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT

public:
    explicit M3u8Interceptor(QObject *parent = nullptr);
    ~M3u8Interceptor() override = default;

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

signals:
    void streamFound(const QString &url);
};

#endif // M3U8INTERCEPTOR_H