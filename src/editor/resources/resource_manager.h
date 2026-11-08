#pragma once
#include <QtCore>
#include <QtGui>
#include <optional>
namespace mdedit {
struct ResourceSnapshot {
    QImage image;
    int page=0;
    int pageCount=0;
    QList<QStringList> rows;
    QString error;
    std::optional<QString> text;
    bool textTruncated=false;
};
class ResourceResolver {
public:
    static bool isImageResource(const QUrl &url);
    static bool isPreviewResource(const QUrl &url);
    static bool isTextResource(const QUrl &url);
    static QUrl resolve(const QString &uri,const QUrl &base);
};
// Worker results contain CPU data only; cache ownership and publication stay on GUI thread.
class ResourceManager : public QObject {
    Q_OBJECT
    QThreadPool pool;
    QCache<QString,ResourceSnapshot> cache{64*1024}; // KiB
    QSet<QString> pending;
    quint64 revision=0;
public:
    explicit ResourceManager(QObject *parent=nullptr);
    ~ResourceManager() override;
    QString request(const QUrl &url,int page=0);
    const ResourceSnapshot *lookup(const QString &key) const {return cache.object(key);}
    void clear();
signals:
    void ready();
};
}
