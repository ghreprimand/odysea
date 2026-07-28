#include "directory_list_model.hpp"
#include "thumbnail_image_provider.hpp"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, std::string_view contents = "image fixture") {
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

QString providerId(const QString& source) {
    return source.mid(QStringLiteral("image://odysea-thumbnail/").size());
}

odysea::core::ThumbnailImage solidImage(std::byte red, std::byte green) {
    return odysea::core::ThumbnailImage{.pixels = {red, green, std::byte{0}, std::byte{255}, red,
                                                   green, std::byte{0}, std::byte{255}, red, green,
                                                   std::byte{0}, std::byte{255}, red, green,
                                                   std::byte{0}, std::byte{255}},
                                        .width = 2,
                                        .height = 2};
}

class RecordingProducer final : public odysea::core::ThumbnailProducer {
  public:
    explicit RecordingProducer(bool blockFirst = false) : blockFirst_(blockFirst) {}

    odysea::core::ThumbnailImage produce(const fs::path& source, odysea::core::ThumbnailSize,
                                         std::error_code& error) override {
        std::unique_lock lock(mutex_);
        const int call = ++calls_;
        sources_.push_back(source);
        changed_.notify_all();
        if (blockFirst_ && call == 1) {
            changed_.wait(lock, [this] { return firstReleased_; });
        }
        ++finished_;
        changed_.notify_all();
        error.clear();
        return source.filename() == "old.png" ? solidImage(std::byte{255}, std::byte{0})
                                              : solidImage(std::byte{0}, std::byte{255});
    }

    [[nodiscard]] int calls() const {
        const std::lock_guard lock(mutex_);
        return calls_;
    }

    [[nodiscard]] int finished() const {
        const std::lock_guard lock(mutex_);
        return finished_;
    }

    void releaseFirst() {
        const std::lock_guard lock(mutex_);
        firstReleased_ = true;
        changed_.notify_all();
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<fs::path> sources_;
    int calls_ = 0;
    int finished_ = 0;
    bool blockFirst_ = false;
    bool firstReleased_ = false;
};

class EmptyStore final : public odysea::core::ThumbnailStore {
  public:
    std::optional<odysea::core::StoredThumbnail> load(const odysea::core::ThumbnailKey&,
                                                      std::error_code& error) override {
        error.clear();
        return std::nullopt;
    }

    void save(const odysea::core::ThumbnailKey&, const odysea::core::ThumbnailImage&,
              std::error_code& error) override {
        error.clear();
    }
};

odysea::core::ThumbnailServiceOptions testOptions() {
    return {
        .worker_count = 1, .memory_budget_bytes = 1024, .queue_bound = 16, .failure_memory = 16};
}

} // namespace

class ThumbnailModelTest : public QObject {
    Q_OBJECT

  private slots:
    void rolesUseStableKeysAndDisappearWithEntries();
    void staleResultCannotAttachToReusedRow();
    void releasedRequestDoesNotPublish();
    void completedThumbnailIsRemovedOnRelease();
    void providerEvictsLeastRecentlyUsedImagesByByteCost();
    void changedVisibleFileIsRequestedAgain();
    void symlinkUsesResolvedTargetMetadata();
    void queuedDeliveryIsSafeAcrossDestruction();
};

void ThumbnailModelTest::rolesUseStableKeysAndDisappearWithEntries() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    writeFile(root / "original.png");
    std::error_code linkError;
    fs::create_hard_link(root / "original.png", root / "linked.png", linkError);
    QVERIFY(!linkError);

    ThumbnailImageProvider provider;
    RecordingProducer producer;
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.rowCount() > 0);
    QCOMPARE(model.roleNames().value(DirectoryListModel::ThumbnailSourceRole),
             QByteArrayLiteral("thumbnailSource"));
    QCOMPARE(model.roleNames().value(DirectoryListModel::ThumbnailLoadingRole),
             QByteArrayLiteral("thumbnailLoading"));

    const int originalRow = rowForName(model, QStringLiteral("original.png"));
    const int linkedRow = rowForName(model, QStringLiteral("linked.png"));
    QVERIFY(originalRow >= 0);
    QVERIFY(linkedRow >= 0);
    model.requestThumbnail(originalRow);
    model.requestThumbnail(linkedRow);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(originalRow), DirectoryListModel::ThumbnailSourceRole)
             .toString()
             .isEmpty(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(linkedRow), DirectoryListModel::ThumbnailSourceRole)
             .toString()
             .isEmpty(),
        5000);

    const QString originalSource =
        model.data(model.index(originalRow), DirectoryListModel::ThumbnailSourceRole).toString();
    const QString linkedSource =
        model.data(model.index(linkedRow), DirectoryListModel::ThumbnailSourceRole).toString();
    QVERIFY(originalSource != linkedSource);
    QVERIFY(!provider.requestImage(providerId(originalSource), nullptr, {}).isNull());
    QVERIFY(!provider.requestImage(providerId(linkedSource), nullptr, {}).isNull());

    fs::remove(root / "original.png");
    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
    QCOMPARE(model.data(model.index(0), DirectoryListModel::NameRole).toString(),
             QStringLiteral("linked.png"));
    QVERIFY(provider.requestImage(providerId(originalSource), nullptr, {}).isNull());
    QVERIFY(!provider.requestImage(providerId(linkedSource), nullptr, {}).isNull());
}

void ThumbnailModelTest::staleResultCannotAttachToReusedRow() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    fs::create_directories(root / "old");
    fs::create_directories(root / "new");
    writeFile(root / "old" / "old.png");
    writeFile(root / "new" / "new.png");

    ThumbnailImageProvider provider;
    RecordingProducer producer(true);
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(QString::fromStdString((root / "old").string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.rowCount() > 0);
    model.requestThumbnail(0);
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 1, 5000);

    model.setPath(QString::fromStdString((root / "new").string()));
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DirectoryListModel::NameRole).toString(),
             QStringLiteral("new.png"));
    model.requestThumbnail(0);
    producer.releaseFirst();

    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty(),
        5000);
    const QString source =
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString();
    const QImage image = provider.requestImage(providerId(source), nullptr, {});
    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(0, 0), QColor(0, 255, 0, 255));
}

void ThumbnailModelTest::releasedRequestDoesNotPublish() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFile(fs::path(fixture.path().toStdString()) / "old.png");

    ThumbnailImageProvider provider;
    RecordingProducer producer(true);
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.rowCount() > 0);

    model.requestThumbnail(0);
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 1, 5000);
    const QString entryPath = model.data(model.index(0), DirectoryListModel::PathRole).toString();
    model.releaseThumbnail(entryPath);
    QVERIFY(!model.data(model.index(0), DirectoryListModel::ThumbnailLoadingRole).toBool());
    producer.releaseFirst();
    QTRY_COMPARE_WITH_TIMEOUT(producer.finished(), 1, 5000);
    QTest::qWait(20);
    QVERIFY(
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty());
}

void ThumbnailModelTest::completedThumbnailIsRemovedOnRelease() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFile(fs::path(fixture.path().toStdString()) / "picture.png");

    ThumbnailImageProvider provider;
    RecordingProducer producer;
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);

    model.requestThumbnail(0);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty(),
        5000);
    const QString entryPath = model.data(model.index(0), DirectoryListModel::PathRole).toString();
    const QString source =
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString();
    const QString id = providerId(source);
    QVERIFY(!provider.requestImage(id, nullptr, {}).isNull());

    model.releaseThumbnail(entryPath);
    QVERIFY(
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty());
    QVERIFY(!model.data(model.index(0), DirectoryListModel::ThumbnailLoadingRole).toBool());
    QVERIFY(provider.requestImage(id, nullptr, {}).isNull());
}

void ThumbnailModelTest::providerEvictsLeastRecentlyUsedImagesByByteCost() {
    QImage first(QSize{2, 2}, QImage::Format_RGBA8888);
    QImage second(QSize{2, 2}, QImage::Format_RGBA8888);
    QImage third(QSize{2, 2}, QImage::Format_RGBA8888);
    first.fill(Qt::red);
    second.fill(Qt::green);
    third.fill(Qt::blue);
    QCOMPARE(first.sizeInBytes(), 16);

    ThumbnailImageProvider provider(32);
    QVERIFY(provider.insert(QStringLiteral("first"), first).retained);
    QVERIFY(provider.insert(QStringLiteral("second"), second).retained);
    QVERIFY(!provider.requestImage(QStringLiteral("first"), nullptr, {}).isNull());

    const ThumbnailImageProvider::InsertResult insertion =
        provider.insert(QStringLiteral("third"), third);
    QVERIFY(insertion.retained);
    QCOMPARE(insertion.evictedIds, QStringList{QStringLiteral("second")});
    QVERIFY(!provider.requestImage(QStringLiteral("first"), nullptr, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("second"), nullptr, {}).isNull());
    QVERIFY(!provider.requestImage(QStringLiteral("third"), nullptr, {}).isNull());
}

void ThumbnailModelTest::changedVisibleFileIsRequestedAgain() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path picture = fs::path(fixture.path().toStdString()) / "picture.png";
    writeFile(picture);

    ThumbnailImageProvider provider;
    RecordingProducer producer;
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(model.rowCount(), 1);

    model.requestThumbnail(0);
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty(),
        5000);
    const QString originalSource =
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString();

    writeFile(picture, "changed image fixture with a different byte length");
    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString().isEmpty(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        model.data(model.index(0), DirectoryListModel::ThumbnailSourceRole).toString() !=
            originalSource,
        5000);
}

void ThumbnailModelTest::symlinkUsesResolvedTargetMetadata() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const fs::path root = fixture.path().toStdString();
    const fs::path target = root / "target.png";
    const fs::path link = root / "linked.png";
    writeFile(target);
    std::error_code linkError;
    fs::create_symlink(target, link, linkError);
    QVERIFY(!linkError);

    ThumbnailImageProvider provider;
    RecordingProducer producer;
    EmptyStore store;
    DirectoryListModel model(provider, producer, store, testOptions());
    model.setPath(fixture.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    const int linkedRow = rowForName(model, QStringLiteral("linked.png"));
    QVERIFY(linkedRow >= 0);

    model.requestThumbnail(linkedRow);
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(linkedRow), DirectoryListModel::ThumbnailSourceRole)
             .toString()
             .isEmpty(),
        5000);
    const QString originalSource =
        model.data(model.index(linkedRow), DirectoryListModel::ThumbnailSourceRole).toString();

    writeFile(target, "replacement target contents with a different size");
    model.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(producer.calls(), 2, 5000);
    const int refreshedLinkedRow = rowForName(model, QStringLiteral("linked.png"));
    QVERIFY(refreshedLinkedRow >= 0);
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(refreshedLinkedRow), DirectoryListModel::ThumbnailSourceRole)
             .toString()
             .isEmpty(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        model.data(model.index(refreshedLinkedRow), DirectoryListModel::ThumbnailSourceRole)
                .toString() != originalSource,
        5000);
}

void ThumbnailModelTest::queuedDeliveryIsSafeAcrossDestruction() {
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFile(fs::path(fixture.path().toStdString()) / "picture.png");

    ThumbnailImageProvider provider;
    RecordingProducer producer;
    EmptyStore store;
    {
        auto model = std::make_unique<DirectoryListModel>(provider, producer, store, testOptions());
        model->setPath(fixture.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(model->rowCount() > 0);
        model->requestThumbnail(0);
        QTRY_COMPARE_WITH_TIMEOUT(producer.finished(), 1, 5000);
    }
    QCoreApplication::processEvents();
}

QTEST_GUILESS_MAIN(ThumbnailModelTest)

#include "tst_thumbnail_model.moc"
