#include "qml/LicenseModel.h"

#include <QSet>

#include <gtest/gtest.h>

// Guards the generated third-party license inventory (see
// scripts/gen_third_party_notices.py), not the model's rendering glue. Every
// assertion below covers a drift that compiles cleanly, is invisible until
// someone opens the dialog, and is a compliance problem when it happens --
// which is why this one src/qml model gets a test at all.
//
// Reads the working copy's licenses/ through the baseDir constructor;
// MegaExplorerTests embeds no qrc, and the generated files are the subject
// anyway.
namespace
{

// A directory rather than a ready-made model: LicenseModel is a QObject, so it
// can't be handed back by value.
QString licensesDir()
{
    return QStringLiteral(MEGAEXPLORER_LICENSE_DIR);
}

QString roleString(const LicenseModel& model, int row, int role)
{
    return model.data(model.index(row, 0), role).toString();
}

} // namespace

TEST(LicenseModelTest, ListsTheAppItselfFirst)
{
    const LicenseModel model(licensesDir());

    ASSERT_GT(model.rowCount(), 0);
    // The dialog opens on row 0, so this app's own MIT text is what it shows first.
    EXPECT_EQ(roleString(model, 0, LicenseModel::NameRole), QStringLiteral("MegaExplorer"));
}

TEST(LicenseModelTest, EveryComponentHasNonEmptyLicenseText)
{
    const LicenseModel model(licensesDir());

    // The one that matters: a manifest entry whose text file is missing is also
    // missing from the qrc, and the dialog silently shows a blank pane.
    for (int row = 0; row < model.rowCount(); ++row)
    {
        EXPECT_FALSE(model.licenseText(row).isEmpty())
            << "empty license text for "
            << roleString(model, row, LicenseModel::NameRole).toStdString();
    }
}

TEST(LicenseModelTest, EveryComponentIsFullyIdentified)
{
    const LicenseModel model(licensesDir());

    for (int row = 0; row < model.rowCount(); ++row)
    {
        const std::string name = roleString(model, row, LicenseModel::NameRole).toStdString();
        EXPECT_FALSE(name.empty()) << "unnamed component at row " << row;
        EXPECT_FALSE(roleString(model, row, LicenseModel::VersionRole).isEmpty()) << name;
        EXPECT_FALSE(roleString(model, row, LicenseModel::HomepageRole).isEmpty()) << name;

        const QString license = roleString(model, row, LicenseModel::LicenseRole);
        EXPECT_FALSE(license.isEmpty()) << name;
        // vcpkg's placeholder for "could not reduce this to an SPDX expression".
        // Reaching the UI means the generator's override table lost an entry.
        EXPECT_FALSE(license.contains(QStringLiteral("LicenseRef-"))) << name;
    }
}

TEST(LicenseModelTest, ComponentNamesAreUnique)
{
    const LicenseModel model(licensesDir());

    QSet<QString> names;
    for (int row = 0; row < model.rowCount(); ++row)
        names.insert(roleString(model, row, LicenseModel::NameRole));
    // A duplicate means two ports collapsed onto one id and one text was lost.
    EXPECT_EQ(static_cast<int>(names.size()), model.rowCount());
}

TEST(LicenseModelTest, ExposesTheRoleNamesQmlBindsTo)
{
    const LicenseModel model(licensesDir());
    const QHash<int, QByteArray> roles = model.roleNames();

    EXPECT_EQ(roles.value(LicenseModel::NameRole), QByteArray("name"));
    EXPECT_EQ(roles.value(LicenseModel::VersionRole), QByteArray("version"));
    EXPECT_EQ(roles.value(LicenseModel::LicenseRole), QByteArray("license"));
    EXPECT_EQ(roles.value(LicenseModel::HomepageRole), QByteArray("homepage"));
}

TEST(LicenseModelTest, OutOfRangeRowsYieldEmptyText)
{
    const LicenseModel model(licensesDir());

    EXPECT_TRUE(model.licenseText(-1).isEmpty());
    EXPECT_TRUE(model.licenseText(model.rowCount()).isEmpty());
}

TEST(LicenseModelTest, MissingManifestYieldsEmptyModel)
{
    // Not a crash and not an assert: the app has to start even if its resources
    // were mangled, same policy as MenuActions::forSite's unknown site.
    const LicenseModel model(QStringLiteral("/no/such/directory"));

    EXPECT_EQ(model.rowCount(), 0);
}
