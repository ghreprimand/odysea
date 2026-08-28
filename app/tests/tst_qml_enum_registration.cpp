#include "fuzzy_find_model.hpp"
#include "miller_columns_model.hpp"
#include "storage_usage_model.hpp"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QtTest>

#include <type_traits>

using odysea::app::FuzzyFindModel;
using odysea::app::MillerColumnsModel;
using odysea::app::StorageUsageModel;

class QmlEnumRegistrationTest : public QObject {
    Q_OBJECT

  private slots:
    void modelRoleEnumsUseQmlCompatibleStorage();
    void modelRoleEnumsAreVisibleToQml();
};

void QmlEnumRegistrationTest::modelRoleEnumsUseQmlCompatibleStorage() {
    QVERIFY((std::is_same_v<std::underlying_type_t<FuzzyFindModel::Roles>, int>));
    QVERIFY((std::is_same_v<std::underlying_type_t<MillerColumnsModel::Roles>, int>));
    QVERIFY((std::is_same_v<std::underlying_type_t<StorageUsageModel::Roles>, int>));
}

void QmlEnumRegistrationTest::modelRoleEnumsAreVisibleToQml() {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import OdySea

        QtObject {
            property int fuzzyNameRole: FuzzyFindModel.NameRole
            property int fuzzySelectedRole: FuzzyFindModel.SelectedRole
            property int millerListingRole: MillerColumnsModel.ListingModelRole
            property int millerDepthRole: MillerColumnsModel.DepthRole
            property int usageNameRole: StorageUsageModel.NameRole
            property int usageSelectedRole: StorageUsageModel.SelectedRole
        }
    )",
                      QUrl(QStringLiteral("inline:QmlEnumRegistrationTest")));

    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("fuzzyNameRole").toInt(), static_cast<int>(FuzzyFindModel::NameRole));
    QCOMPARE(object->property("fuzzySelectedRole").toInt(),
             static_cast<int>(FuzzyFindModel::SelectedRole));
    QCOMPARE(object->property("millerListingRole").toInt(),
             static_cast<int>(MillerColumnsModel::ListingModelRole));
    QCOMPARE(object->property("millerDepthRole").toInt(),
             static_cast<int>(MillerColumnsModel::DepthRole));
    QCOMPARE(object->property("usageNameRole").toInt(),
             static_cast<int>(StorageUsageModel::NameRole));
    QCOMPARE(object->property("usageSelectedRole").toInt(),
             static_cast<int>(StorageUsageModel::SelectedRole));
}

QTEST_GUILESS_MAIN(QmlEnumRegistrationTest)

#include "tst_qml_enum_registration.moc"
