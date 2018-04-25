#ifndef WEBAPPMANAGERSERVICEAGL_H
#define WEBAPPMANAGERSERVICEAGL_H

#include <QJsonObject>

#include "WebAppManagerService.h"

class WebAppManagerServiceAGL : public WebAppManagerService {
public:
    static WebAppManagerService* instance();

    bool startService();

    // WebAppManagerService
    QJsonObject launchApp(QJsonObject request) override;
    QJsonObject killApp(QJsonObject request) override;
    QJsonObject logControl(QJsonObject request) override;
    QJsonObject setInspectorEnable(QJsonObject request) override;
    QJsonObject closeAllApps(QJsonObject request) override;
    QJsonObject discardCodeCache(QJsonObject request) override;
    QJsonObject listRunningApps(QJsonObject request, bool subscribed) override;
    QJsonObject closeByProcessId(QJsonObject request) override;
    QJsonObject getWebProcessSize(QJsonObject request) override;
    QJsonObject clearBrowsingData(QJsonObject request) override;
    QJsonObject webProcessCreated(QJsonObject request, bool subscribed) override;

private:

    WebAppManagerServiceAGL();
    ~WebAppManagerServiceAGL();
};

#endif // WEBAPPMANAGERSERVICEAGL_H
