// m3u8interceptor.h
#ifndef M3U8INTERCEPTOR_H
#define M3U8INTERCEPTOR_H

#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QString>
#include <QList>
#include <QWebEngineHttpRequestHeader>

class M3u8Interceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT

public:
    explicit M3u8Interceptor(QObject *parent = nullptr);
    ~M3u8Interceptor() override = default;

    struct LicenseRequest {
        QString url;
        QString headers;
        bool isValid;
    };

    LicenseRequest getLastLicenseRequest() const { return m_lastLicenseReq; }

protected:
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

signals:
    void streamFound(const QString &url);

private:
    LicenseRequest m_lastLicenseReq;
    QString convertHeaders(const QList<QWebEngineHttpRequestHeader> &headers) const;
};

#endif // M3U8INTERCEPTOR_H