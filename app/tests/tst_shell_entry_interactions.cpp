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

QTEST_MAIN(ShellEntryInteractionsTest)

#include "tst_shell_entry_interactions.moc"
