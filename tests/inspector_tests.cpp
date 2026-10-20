#include "ui/InspectorSummary.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest>

namespace {

QJsonObject asset(const QString &id = QStringLiteral("example-a"))
{
    return {{"id", id}, {"revision", 1}, {"name", QString::fromUtf8("归巢装置.glb")},
            {"extension", "glb"}, {"type", "model"}, {"size", 2516582},
            {"source_path", QString::fromUtf8(__FILE__)}, {"missing", false},
            {"backup_state", "none"}, {"favorite", false},
            {"tags", QJsonArray{QString::fromUtf8("三维模型"), QString::fromUtf8("工作区示范")}}};
}

QJsonArray pages()
{
    return {QJsonObject{{"key", "cover"}, {"label", QString::fromUtf8("模型封面")}},
            QJsonObject{{"key", "front"}, {"label", QString::fromUtf8("正面")}},
            QJsonObject{{"key", "detail"}, {"label", QString::fromUtf8("细节")}}};
}

QAbstractButton *button(QWidget &widget, const char *name)
{
    return widget.findChild<QAbstractButton *>(QString::fromLatin1(name));
}

void settle(QWidget &widget)
{
    widget.ensurePolished();
    if (widget.layout()) widget.layout()->activate();
    QCoreApplication::processEvents();
}

QImage raster(QWidget &widget)
{
    QImage result(widget.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    widget.render(&result);
    return result;
}

bool hasGreenPixels(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.green() > 170 && color.red() < 60 && color.blue() < 60) return true;
        }
    return false;
}

bool containsColor(const QImage &image, const QColor &expected)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (qAbs(color.red() - expected.red()) <= 2 &&
                qAbs(color.green() - expected.green()) <= 2 &&
                qAbs(color.blue() - expected.blue()) <= 2) return true;
        }
    return false;
}

// A plain themed host supplies the background that transparent previews must expose.
// These artifacts are QWidget raster renders on the offscreen platform, not desktop captures.
class ThemeHost final : public QWidget {
public:
    explicit ThemeHost(QWidget *parent = nullptr) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto colors = themeColors();
        QPainter painter(this);
        QLinearGradient background(rect().topLeft(), rect().bottomRight());
        background.setColorAt(0, colors.top);
        background.setColorAt(1, colors.bottom);
        painter.fillRect(rect(), background);
        QRadialGradient glow(QPointF(width() * .15, height() * .1), width() * 1.1);
        QColor edge = colors.glow;
        edge.setAlpha(60);
        glow.setColorAt(0, edge);
        edge.setAlpha(0);
        glow.setColorAt(1, edge);
        painter.fillRect(rect(), glow);
    }
};

} // namespace

class InspectorTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QApplication::setApplicationName(QStringLiteral("ImagiDock Inspector Tests"));
        QApplication::setOrganizationName(QStringLiteral("ImagiDock"));
        QVERIFY2(QGuiApplication::platformName() == QStringLiteral("offscreen"),
                 "Run this focused widget test with QT_QPA_PLATFORM=offscreen; it does not open the desktop application.");
        const QString fontPath = qEnvironmentVariable("IMAGIDOCK_INSPECTOR_FONT");
        if (!fontPath.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFont(fontPath);
            QVERIFY2(fontId >= 0, qPrintable(QStringLiteral("Cannot load the requested offscreen font: %1").arg(fontPath)));
            const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            QVERIFY2(!families.isEmpty(), "The requested offscreen font has no usable family.");
            QFont font(families.first());
            font.setPixelSize(13);
            QApplication::setFont(font);
        }
    }

    void carouselButtonsAndDotsIgnoreLateImages()
    {
        InspectorSummary summary;
        summary.resize(360, 720);
        summary.setAsset(asset());
        summary.setPreviewPages(pages());
        summary.show();
        settle(summary);
        QSignalSpy selected(&summary, &InspectorSummary::previewSelected);
        QVERIFY(selected.isValid());
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("cover"));

        auto next = button(summary, "inspectorPreviewNext");
        auto previous = button(summary, "inspectorPreviewPrevious");
        auto dot = button(summary, "inspectorPreviewDot_2");
        QVERIFY(next && previous && dot);
        QVERIFY(next->isVisible() && next->isEnabled());
        QTest::mouseClick(next, Qt::LeftButton);
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("front"));
        QCOMPARE(selected.last().at(0).toString(), QStringLiteral("front"));
        QTest::mouseClick(previous, Qt::LeftButton);
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("cover"));
        QTest::mouseClick(dot, Qt::LeftButton);
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("detail"));
        QCOMPARE(selected.last().at(0).toString(), QStringLiteral("detail"));
        QVERIFY(dot->isChecked());
        QCOMPARE(selected.count(), 3);

        QPixmap current(80, 80);
        current.fill(QColor(20, 230, 20));
        summary.setPreviewImage(QStringLiteral("detail"), current);
        settle(summary);
        auto carousel = summary.findChild<QWidget *>(QStringLiteral("inspectorCarousel"));
        QVERIFY(carousel);
        const QImage beforeLateResult = raster(*carousel);
        QVERIFY2(hasGreenPixels(beforeLateResult), "The selected page must actually display the delivered image.");
        QPixmap late(80, 80);
        late.fill(Qt::red);
        summary.setPreviewImage(QStringLiteral("cover"), late);
        summary.setPreviewError(QStringLiteral("front"), QString::fromUtf8("旧页面预览解码失败"));
        settle(summary);
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("detail"));
        QCOMPARE(raster(*carousel), beforeLateResult);
    }

    void metadataRefreshPreservesPageAndNewAssetResetsIt()
    {
        InspectorSummary summary;
        summary.resize(360, 720);
        auto original = asset();
        summary.setAsset(original);
        summary.setPreviewPages(pages());
        summary.show();
        settle(summary);
        auto dot = button(summary, "inspectorPreviewDot_1");
        QVERIFY(dot);
        QTest::mouseClick(dot, Qt::LeftButton);
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("front"));

        original["revision"] = 2;
        original["favorite"] = true;
        original["tags"] = QJsonArray{QString::fromUtf8("新增标签")};
        summary.setAsset(original);
        summary.setPreviewPages(pages());
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("front"));
        summary.setAsset(asset(QStringLiteral("example-b")));
        summary.setPreviewPages(pages());
        QCOMPARE(summary.currentPreviewKey(), QStringLiteral("cover"));

        summary.setPreviewPages(QJsonArray{pages().at(0)});
        settle(summary);
        for (const char *name : {"inspectorPreviewPrevious", "inspectorPreviewNext"}) {
            auto arrow = button(summary, name);
            QVERIFY(arrow);
            QVERIFY2(!arrow->isVisible() || !arrow->isEnabled(), "A single preview must not expose usable previous/next actions.");
        }
    }

    void favoriteAndTagActions()
    {
        InspectorSummary summary;
        summary.resize(360, 720);
        summary.setAsset(asset());
        summary.setPreviewPages(pages());
        summary.show();
        settle(summary);
        auto favorite = button(summary, "inspectorFavorite");
        auto addTag = button(summary, "inspectorAddTag");
        auto tag = button(summary, "inspectorTag_0");
        QVERIFY(favorite && addTag && tag);
        QVERIFY2(favorite->text().isEmpty(), "Favorite is an icon action; visible 收藏 text must not take title space.");
        QVERIFY(!favorite->accessibleName().isEmpty());
        QVERIFY(!addTag->accessibleName().isEmpty());
        QSignalSpy favorites(&summary, &InspectorSummary::favoriteToggled);
        QSignalSpy additions(&summary, &InspectorSummary::addTagRequested);
        QSignalSpy removals(&summary, &InspectorSummary::removeTagRequested);
        QVERIFY(favorites.isValid() && additions.isValid() && removals.isValid());
        QTest::mouseClick(favorite, Qt::LeftButton);
        QCOMPARE(favorites.count(), 1);
        QCOMPARE(favorites.at(0).at(0).toBool(), true);
        QTest::mouseClick(favorite, Qt::LeftButton);
        QCOMPARE(favorites.count(), 2);
        QCOMPARE(favorites.at(1).at(0).toBool(), false);
        QTest::mouseClick(addTag, Qt::LeftButton);
        QCOMPARE(additions.count(), 1);
        QCOMPARE(tag->property("tagName").toString(), QString::fromUtf8("三维模型"));
        const QString originalTagText = tag->text();
        tag->setFocus();
        QTest::keyClick(tag, Qt::Key_Delete);
        QCOMPARE(removals.count(), 1);
        QCOMPARE(removals.at(0).at(0).toString(), QString::fromUtf8("三维模型"));
        QCOMPARE(tag->text(), originalTagText);
        QVERIFY(tag->isVisible());
        // The chip remains until a successful library update returns new metadata.
        auto updated = asset();
        updated["revision"] = 2;
        updated["tags"] = QJsonArray{QString::fromUtf8("工作区示范")};
        summary.setAsset(updated);
        QCOMPARE(button(summary, "inspectorTag_0")->text(), QString::fromUtf8("工作区示范"));
    }

    void readableStatusSemantics_data()
    {
        QTest::addColumn<bool>("missing");
        QTest::addColumn<QString>("backup");
        QTest::addColumn<bool>("corrupt");
        QTest::addColumn<bool>("backupValid");
        QTest::addColumn<QString>("backupText");
        QTest::newRow("source-ready-unbacked") << false << QStringLiteral("none") << false << false << QString::fromUtf8("尚未备份");
        QTest::newRow("source-ready-backed") << false << QStringLiteral("ready") << false << true << QString::fromUtf8("已备份");
        QTest::newRow("source-missing-backed") << true << QStringLiteral("ready") << false << true << QString::fromUtf8("已备份");
        QTest::newRow("source-missing-unbacked") << true << QStringLiteral("none") << false << false << QString::fromUtf8("尚未备份");
        QTest::newRow("backup-outdated-still-available") << false << QStringLiteral("stale") << false << true << QString::fromUtf8("已备份");
        QTest::newRow("backup-corrupt") << false << QStringLiteral("ready") << true << false << QString::fromUtf8("备份失效");
    }

    void readableStatusSemantics()
    {
        QFETCH(bool, missing);
        QFETCH(QString, backup);
        QFETCH(bool, corrupt);
        QFETCH(bool, backupValid);
        QFETCH(QString, backupText);
        InspectorSummary summary;
        auto record = asset();
        record["missing"] = missing;
        record["backup_state"] = backup;
        if (corrupt)
            record["issues"] = QJsonArray{QJsonObject{{"kind", "backup_corrupt"}}};
        summary.setAsset(record);
        auto source = summary.findChild<QWidget *>(QStringLiteral("inspectorSourceStatus"));
        auto stored = summary.findChild<QWidget *>(QStringLiteral("inspectorBackupStatus"));
        QVERIFY(source && stored);
        QVERIFY(source->property("valid").isValid());
        QVERIFY(stored->property("valid").isValid());
        QCOMPARE(source->property("valid").toBool(), !missing);
        QCOMPARE(stored->property("valid").toBool(), backupValid);
        QVERIFY(source->property("text").toString().contains(missing ? QString::fromUtf8("失效") : QString::fromUtf8("有效")));
        QVERIFY(stored->property("text").toString().contains(backupText));
        QVERIFY(!source->accessibleName().isEmpty());
        QVERIFY(!stored->accessibleName().isEmpty());
        // Accessible text accompanies the green-check/red-invalid treatment.
        // Color is therefore not the only way to distinguish these states.
        QVERIFY(source->accessibleName().contains(source->property("text").toString()));
        QVERIFY(stored->accessibleName().contains(stored->property("text").toString()));
    }

    void narrowContentAndEdgeAlignedPreview_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("minimum-sidebar") << 225;
        QTest::newRow("wide-sidebar") << 360;
    }

    void narrowContentAndEdgeAlignedPreview()
    {
        QFETCH(int, width);
        InspectorSummary summary;
        auto record = asset();
        record["name"] = QString::fromUtf8("森林遗迹中的归巢装置与远方灯塔之超长中文素材名称.glb");
        record["tags"] = QJsonArray{QString::fromUtf8("三维模型"), QString::fromUtf8("工作区示范"),
            QString::fromUtf8("森林遗迹的超长中文标签用于确认窄侧栏可以换行且不会横向溢出"),
            QString::fromUtf8("法线贴图"), QString::fromUtf8("固有色贴图"), QString::fromUtf8("角色武器设计图稿")};
        summary.setAsset(record);
        summary.setPreviewPages(pages());
        summary.resize(width, 900);
        summary.show();
        settle(summary);
        QCOMPARE(summary.width(), width);
        auto carousel = summary.findChild<QWidget *>(QStringLiteral("inspectorCarousel"));
        auto title = summary.findChild<QLabel *>(QStringLiteral("inspectorAssetTitle"));
        auto tags = summary.findChild<QWidget *>(QStringLiteral("inspectorTags"));
        auto format = summary.findChild<QLabel *>(QStringLiteral("inspectorFormat"));
        auto fileSize = summary.findChild<QWidget *>(QStringLiteral("inspectorFileSize"));
        auto source = summary.findChild<QWidget *>(QStringLiteral("inspectorSourceStatus"));
        auto backup = summary.findChild<QWidget *>(QStringLiteral("inspectorBackupStatus"));
        auto addTag = button(summary, "inspectorAddTag");
        QVERIFY(carousel && title && tags && format && fileSize && source && backup && addTag);
        QCOMPARE(carousel->mapTo(&summary, QPoint()), QPoint(0, 0));
        QCOMPARE(carousel->width(), summary.width());
        QVERIFY(title->font().bold());
        QVERIFY(title->alignment().testFlag(Qt::AlignLeft));
        QVERIFY(title->text().contains(QString::fromUtf8("归巢装置")));
        QCOMPARE(format->text().toUpper(), QStringLiteral("GLB"));
        const QPoint titleOrigin = title->mapTo(&summary, QPoint());
        const QPoint sizeOrigin = fileSize->mapTo(&summary, QPoint());
        const QPoint sourceOrigin = source->mapTo(&summary, QPoint());
        const QPoint backupOrigin = backup->mapTo(&summary, QPoint());
        QCOMPARE(sizeOrigin.x(), titleOrigin.x());
        QVERIFY(sizeOrigin.y() >= titleOrigin.y() + title->height());
        QVERIFY(sourceOrigin.y() >= sizeOrigin.y() + fileSize->height());
        QVERIFY(qAbs(sourceOrigin.y() - backupOrigin.y()) <= 2);
        QVERIFY(backupOrigin.x() > sourceOrigin.x());
        QVERIFY(tags->mapTo(&summary, QPoint()).y() >= sourceOrigin.y() + source->height());
        QVERIFY(tags->height() > addTag->height());
        const auto tagButtons = tags->findChildren<QAbstractButton *>(QRegularExpression(QStringLiteral("^inspectorTag_[0-9]+$")));
        QCOMPARE(tagButtons.size(), 6);
        bool wrapped = false;
        int firstY = -1;
        for (const auto *chip : tagButtons) {
            const QRect bounds(chip->mapTo(tags, QPoint()), chip->size());
            QVERIFY2(bounds.left() >= 0 && bounds.right() < tags->width(), qPrintable(QStringLiteral("Tag escapes the sidebar: %1").arg(chip->property("tagName").toString())));
            if (firstY < 0) firstY = bounds.top();
            wrapped = wrapped || bounds.top() > firstY;
        }
        QVERIFY2(wrapped, "Multiple long tags should wrap within the sidebar.");
        for (const char *name : {"inspectorAssetTitle", "inspectorFormat", "inspectorFileSize", "inspectorFavorite", "inspectorSourceStatus", "inspectorBackupStatus", "inspectorTags"}) {
            const auto child = summary.findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY(child);
            const QRect bounds(child->mapTo(&summary, QPoint()), child->size());
            QVERIFY2(bounds.left() >= 0 && bounds.right() < summary.width(), name);
        }
    }

    void fourThemesOffscreen_data()
    {
        QTest::addColumn<QString>("theme");
        for (const char *theme : {"ivory", "graphite", "ocean", "mint"})
            QTest::newRow(theme) << QString::fromLatin1(theme);
    }

    void fourThemesOffscreen()
    {
        QFETCH(QString, theme);
        qApp->setProperty("imagidockTheme", theme);
        ThemeHost host;
        host.resize(360, 720);
        InspectorSummary summary(&host);
        summary.setGeometry(host.rect());
        summary.setAsset(asset());
        summary.setPreviewPages(pages());
        QPixmap transparentCover(240, 180);
        transparentCover.fill(Qt::transparent);
        QPainter drawing(&transparentCover);
        drawing.setPen(Qt::NoPen);
        drawing.setBrush(themeColors().accent);
        drawing.drawRoundedRect(QRectF(65, 20, 110, 140), 12, 12);
        drawing.setBrush(themeColors().text);
        drawing.drawEllipse(QRectF(100, 55, 40, 40));
        drawing.end();
        summary.setPreviewImage(QStringLiteral("cover"), transparentCover);
        host.show();
        settle(summary);
        const auto rendered = raster(host);
        QVERIFY(!rendered.isNull());
        const QColor background = rendered.pixelColor(5, 5);
        QVERIFY(background.alpha() > 0);
        QCOMPARE(themeColors().id, theme);
        QVERIFY2(containsColor(rendered, themeColors().accent), "The transparent cover should be painted in the selected theme.");

        const QString directory = qEnvironmentVariable("IMAGIDOCK_INSPECTOR_ARTIFACT_DIR");
        if (!directory.isEmpty()) {
            const QString project = QDir::cleanPath(QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(".."));
            const QString target = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
            QVERIFY2(target.startsWith(project + "/", Qt::CaseInsensitive), "Offscreen artifacts must stay inside the project directory.");
            QVERIFY(QDir().mkpath(target));
            QVERIFY(rendered.save(target + QStringLiteral("/inspector-%1-offscreen.png").arg(theme)));
        }
    }
};

QTEST_MAIN(InspectorTests)
#include "inspector_tests.moc"
