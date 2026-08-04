#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

// The third-party license inventory behind qml/components/LicenseDialog.qml,
// read from licenses/manifest.json as embedded by qt_add_qml_module's RESOURCES.
// Both the manifest and the texts it points at are generated -- see
// scripts/gen_third_party_notices.py.
//
// Stateless and dependency-free like MenuActions, so it is a real QML singleton
// the engine instantiates rather than something main.cpp injects.
class LicenseModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        VersionRole,
        LicenseRole,
        HomepageRole,
    };

    explicit LicenseModel(QObject* parent = nullptr);
    // Tests read the working copy's licenses/ off disk instead of the qrc copy.
    // An overload rather than a default argument because QML_SINGLETON needs the
    // default constructor to stay the one the engine can call.
    explicit LicenseModel(const QString& baseDir, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Deliberately not a role: the texts total several hundred KB, and a role
    // would pull all of them in the moment the view builds its delegates. QML
    // calls this once per selection change instead of binding to it.
    Q_INVOKABLE QString licenseText(int row) const;

private:
    struct Component
    {
        QString name;
        QString version;
        QString license;
        QString homepage;
        QString textPath;
    };

    void load();

    QString m_baseDir;
    QVector<Component> m_components;
    mutable QHash<int, QString> m_textCache;
};
