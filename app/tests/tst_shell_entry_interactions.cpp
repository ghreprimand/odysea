// Rendered coverage for entry interactions that depend on the real adapter.
//
// QML-only model stubs can accidentally add notifying dependencies that the
// C++ adapter does not expose. These tests therefore render the production
// list and grid delegates with DirectoryListModel and inspect the attached
// Drag payload in the delegate's own QML context.
#include "directory_list_model.hpp"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariant>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, std::string_view contents = "data") {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

int rowForName(const DirectoryListModel& model, const QString& name) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), DirectoryListModel::NameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

QQuickItem* visualChild(QQuickItem* root, const QString& objectName) {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = visualChild(child, objectName); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

QString dragPayload(QObject* delegate, bool& evaluated) {
    QQmlExpression expression(qmlContext(delegate), delegate,
                              QStringLiteral("Drag.mimeData['text/uri-list']"));
    const QVariant result = expression.evaluate();
    evaluated = !expression.hasError();
    return result.toString();
}

QString encodedUrl(const fs::path& path) {
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString(QUrl::FullyEncoded);
}

} // namespace

class ShellEntryInteractionsTest : public QObject {
    Q_OBJECT

  private slots:
    void dragPayloadTracksRealModelSelectionInBothViews();
    void dragPayloadTracksRenames_data();
    void dragPayloadTracksRenames();
};

void ShellEntryInteractionsTest::dragPayloadTracksRealModelSelectionInBothViews() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const fs::path root = directory.path().toStdString();
    const fs::path alpha = root / "alpha space.txt";
    const fs::path beta = root / "beta#value.txt";
    const fs::path target = root / "target";
    const fs::path targetLink = root / "target-link";
    writeFile(alpha);
    writeFile(beta);
    fs::create_directory(target);
    std::error_code linkError;
    fs::create_directory_symlink(target, targetLink, linkError);
    QVERIFY2(!linkError, linkError.message().c_str());

    DirectoryListModel model;
    model.setPath(directory.path());
    QTRY_COMPARE(model.rowCount(), 4);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("OdySea", "Main");
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const QScopedPointer<QObject> rootObject(component.createWithInitialProperties(
        {{QStringLiteral("shellModel"), QVariant::fromValue(&model)}}));
    QVERIFY2(!rootObject.isNull(), qPrintable(component.errorString()));

    auto* window = qobject_cast<QQuickWindow*>(rootObject.data());
    QVERIFY(window != nullptr);
    QTRY_VERIFY(window->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(window));

    const int alphaRow = rowForName(model, QStringLiteral("alpha space.txt"));
    const int betaRow = rowForName(model, QStringLiteral("beta#value.txt"));
    const int linkRow = rowForName(model, QStringLiteral("target-link"));
    QVERIFY(alphaRow >= 0);
    QVERIFY(betaRow >= 0);
    QVERIFY(linkRow >= 0);

    QQuickItem* listDelegate =
        visualChild(window->contentItem(), QStringLiteral("entryRow-%1").arg(alphaRow));
    QTRY_VERIFY(listDelegate != nullptr);
    bool evaluated = false;
    QCOMPARE(dragPayload(listDelegate, evaluated), QString{});
    QVERIFY(evaluated);

    model.selectRow(alphaRow, Qt::NoModifier);
    const QString alphaPayload = encodedUrl(alpha) + QStringLiteral("\r\n");
    QTRY_COMPARE(dragPayload(listDelegate, evaluated), alphaPayload);
    QVERIFY(evaluated);

    model.selectRow(betaRow, Qt::NoModifier);
    const QString betaPayload = encodedUrl(beta) + QStringLiteral("\r\n");
    QCOMPARE(model.selectedCount(), 1);
    QTRY_COMPARE(dragPayload(listDelegate, evaluated), betaPayload);
    QVERIFY(evaluated);

    model.selectRow(alphaRow, Qt::ControlModifier);
    const QString pairPayload =
        encodedUrl(alpha) + QStringLiteral("\r\n") + encodedUrl(beta) + QStringLiteral("\r\n");
    QTRY_COMPARE(dragPayload(listDelegate, evaluated), pairPayload);
    QVERIFY(evaluated);

    QQuickItem* listDropTarget =
        visualChild(window->contentItem(), QStringLiteral("entryDropTarget-%1").arg(linkRow));
    QTRY_VERIFY(listDropTarget != nullptr);
    QVERIFY(listDropTarget->isEnabled());

    model.clearSelection();
    rootObject->setProperty("gridMode", true);
    QQuickItem* gridDelegate =
        visualChild(window->contentItem(), QStringLiteral("entryCell-%1").arg(betaRow));
    QTRY_VERIFY(gridDelegate != nullptr);
    QCOMPARE(dragPayload(gridDelegate, evaluated), QString{});
    QVERIFY(evaluated);

    model.selectRow(alphaRow, Qt::NoModifier);
    QTRY_COMPARE(dragPayload(gridDelegate, evaluated), alphaPayload);
    QVERIFY(evaluated);
    model.selectRow(betaRow, Qt::NoModifier);
    QCOMPARE(model.selectedCount(), 1);
    QTRY_COMPARE(dragPayload(gridDelegate, evaluated), betaPayload);
    QVERIFY(evaluated);

    QQuickItem* gridDropTarget =
        visualChild(window->contentItem(), QStringLiteral("gridEntryDropTarget-%1").arg(linkRow));
    QTRY_VERIFY(gridDropTarget != nullptr);
    QVERIFY(gridDropTarget->isEnabled());
}

void ShellEntryInteractionsTest::dragPayloadTracksRenames_data() {
    QTest::addColumn<bool>("gridMode");
    QTest::addColumn<bool>("externalRename");

    QTest::newRow("list-in-app") << false << false;
    QTest::newRow("list-watcher") << false << true;
    QTest::newRow("grid-in-app") << true << false;
    QTest::newRow("grid-watcher") << true << true;
}

void ShellEntryInteractionsTest::dragPayloadTracksRenames() {
    QFETCH(bool, gridMode);
    QFETCH(bool, externalRename);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const fs::path root = directory.path().toStdString();
    const fs::path original = root / "before.txt";
    const fs::path renamed = root / "after.txt";
    writeFile(original);

    DirectoryListModel model;
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    const int originalRow = rowForName(model, QStringLiteral("before.txt"));
    QVERIFY(originalRow >= 0);
    model.selectRow(originalRow, Qt::NoModifier);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("OdySea", "Main");
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const QScopedPointer<QObject> rootObject(component.createWithInitialProperties(
        {{QStringLiteral("shellModel"), QVariant::fromValue(&model)},
         {QStringLiteral("gridMode"), gridMode}}));
    QVERIFY2(!rootObject.isNull(), qPrintable(component.errorString()));

    auto* window = qobject_cast<QQuickWindow*>(rootObject.data());
    QVERIFY(window != nullptr);
    QTRY_VERIFY(window->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(window));

    const QString delegateName =
        QStringLiteral("%1-%2")
            .arg(gridMode ? QStringLiteral("entryCell") : QStringLiteral("entryRow"))
            .arg(originalRow);
    QQuickItem* delegate = visualChild(window->contentItem(), delegateName);
    QTRY_VERIFY(delegate != nullptr);
    bool evaluated = false;
    QTRY_COMPARE(dragPayload(delegate, evaluated), encodedUrl(original) + QStringLiteral("\r\n"));
    QVERIFY(evaluated);

    if (externalRename) {
        QTest::qWait(50);
        fs::rename(original, renamed);
    } else {
        model.performRename(QStringLiteral("after.txt"), DirectoryListModel::ConflictFail);
        QTRY_VERIFY_WITH_TIMEOUT(!model.operationBusy(), 5000);
    }

    QTRY_VERIFY_WITH_TIMEOUT(rowForName(model, QStringLiteral("after.txt")) >= 0, 5000);
    QCOMPARE(model.selectedCount(), 1);
    QTRY_COMPARE(dragPayload(delegate, evaluated), encodedUrl(renamed) + QStringLiteral("\r\n"));
    QVERIFY(evaluated);
}

QTEST_MAIN(ShellEntryInteractionsTest)

#include "tst_shell_entry_interactions.moc"
