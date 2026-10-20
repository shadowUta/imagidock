#include "ui/AssetGrid.h"
#include "ui/Theme.h"
#include "ui/UiStyle.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QtTest>

namespace {

const QColor previewInk(215, 30, 159);
const QList<QColor> previewColors{previewInk, QColor(240, 40, 35), QColor(30, 218, 65),
                                 QColor(30, 65, 240), QColor(240, 211, 30)};

bool near(const QColor &actual, const QColor &wanted)
{
    return actual.alpha() > 245 && qAbs(actual.red() - wanted.red()) <= 3 &&
           qAbs(actual.green() - wanted.green()) <= 3 && qAbs(actual.blue() - wanted.blue()) <= 3;
}

QPixmap preview(const QString &shape)
{
    const QSize size = shape == "wide" ? QSize(640, 320) :
                       shape == "tall" ? QSize(320, 640) : QSize(320, 320);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(shape == "alpha" ? Qt::transparent : previewInk);
    if (shape == "alpha") {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(previewInk);
        painter.drawEllipse(QRect(110, 110, 100, 100));
    } else if (shape != "square") {
        // Distinct corner marks expose clipping of either end of a wide or tall image.
        QPainter painter(&image);
        const int mark = 64;
        painter.fillRect(0, 0, mark, mark, previewColors[1]);
        painter.fillRect(size.width() - mark, 0, mark, mark, previewColors[2]);
        painter.fillRect(0, size.height() - mark, mark, mark, previewColors[3]);
        painter.fillRect(size.width() - mark, size.height() - mark, mark, mark, previewColors[4]);
    }
    return QPixmap::fromImage(image);
}

void fillModel(QStandardItemModel &model, int count, const QPixmap &image = preview("square"))
{
    model.clear();
    for (int i = 0; i < count; ++i) {
        auto item = new QStandardItem(QString::fromUtf8("示范资产_%1.png").arg(i + 1));
        item->setData(QJsonObject{{"id", QString("synthetic-%1").arg(i)},
                                 {"name", item->text()}, {"extension", "png"},
                                 {"type", "image"}, {"missing", false},
                                 {"favorite", false}, {"backup_state", "none"}}, AssetModel::AssetRole);
        item->setData(image, Qt::DecorationRole);
        item->setEditable(false);
        model.appendRow(item);
    }
}

QImage viewportStrip(AssetGrid &grid, int height)
{
    QImage result(QSize(grid.viewport()->width(), height), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    grid.viewport()->render(&painter);
    return result;
}

QList<QPair<int, int>> imageRuns(const QImage &image, int y)
{
    QList<QPair<int, int>> runs;
    int start = -1;
    for (int x = 0; x <= image.width(); ++x) {
        const bool colored = x < image.width() && near(image.pixelColor(x, y), previewInk);
        if (colored && start < 0) start = x;
        if (!colored && start >= 0) { runs.append({start, x - 1}); start = -1; }
    }
    return runs;
}

QRect coloredBounds(const QImage &image, const QList<QColor> &colors)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            for (const auto &color : colors)
                if (near(image.pixelColor(x, y), color)) {
                    bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
                    break;
                }
    return bounds;
}

QImage paintedCard(const QString &shape)
{
    QStandardItemModel model;
    fillModel(model, 1, preview(shape));
    AssetDelegate delegate;
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 256, 320);
    option.font = QApplication::font();
    option.font.setPixelSize(13);
    option.fontMetrics = QFontMetrics(option.font);
    option.state = QStyle::State_Enabled;
    QImage result(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    delegate.paint(&painter, option, model.index(0, 0));
    return result;
}

class ResizeCounter final : public QObject {
public:
    int count = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) ++count;
        return false;
    }
};

} // namespace

class GridTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QApplication::setApplicationName(QStringLiteral("ImagiDock Grid Tests"));
        QApplication::setOrganizationName(QStringLiteral("ImagiDock"));
        QApplication::setStyle(createUiStyle());
        QVERIFY2(QGuiApplication::platformName() == "offscreen",
                 "Run with QT_QPA_PLATFORM=offscreen; this test never opens the desktop application.");
        const QString fontPath = qEnvironmentVariable("IMAGIDOCK_GRID_FONT",
                                                      qEnvironmentVariable("IMAGIDOCK_INSPECTOR_FONT"));
        if (!fontPath.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFont(fontPath);
            QVERIFY(fontId >= 0);
            const auto families = QFontDatabase::applicationFontFamilies(fontId);
            QVERIFY(!families.isEmpty());
            QApplication::setFont(QFont(families.first()));
        }
        applyUiTheme("mint");
    }

    void reflow_data()
    {
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("count");
        for (int target : {128, 176, 256, 320})
            for (int count : {12, 200})
                QTest::newRow(qPrintable(QString("target-%1-items-%2").arg(target).arg(count))) << target << count;
    }

    void reflow()
    {
        QFETCH(int, target);
        QFETCH(int, count);
        QStandardItemModel model;
        fillModel(model, count);
        AssetGrid grid;
        grid.setModel(&model);
        grid.setCardSize(target);
        grid.show();
        for (int width : {420, 750, 1000, 1280, 1003, 750, 420}) {
            // Twelve items cross the scrollbar threshold as the viewport narrows;
            // the 200-item case retains scrolling and exercises batched layout.
            grid.resize(width, count == 12 ? 1600 : 480);
            QTRY_VERIFY_WITH_TIMEOUT(grid.visualRect(model.index(count - 1, 0)).isValid(), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(grid.horizontalScrollBar()->maximum(), 0, 3000);
            QTest::qWait(25);
            const QRect first = grid.visualRect(model.index(0, 0));
            QVERIFY(first.isValid());
            QList<QRect> firstRow;
            for (int row = 0; row < count; ++row) {
                const QRect item = grid.visualRect(model.index(row, 0));
                if (item.top() != first.top()) break;
                firstRow.append(item);
                QCOMPARE(grid.indexAt(item.center()).row(), row);
                QVERIFY(item.left() >= 0 && item.right() < grid.viewport()->width());
            }
            QVERIFY(!firstRow.isEmpty());
            QVERIFY2(firstRow.first().left() <= 2, "The first grid cell has an unnecessary leading gutter.");
            const int remaining = grid.viewport()->width() - firstRow.last().right() - 1;
            QVERIFY2(remaining >= 0 && remaining < 12,
                     qPrintable(QString("A complete row leaves %1 px unused at width %2.").arg(remaining).arg(width)));

            const int y = first.top() + first.width() / 2;
            const auto runs = imageRuns(viewportStrip(grid, y + 1), y);
            QCOMPARE(runs.size(), firstRow.size());
            const int leading = runs.first().first;
            const int trailing = grid.viewport()->width() - runs.last().second - 1;
            QVERIFY(leading <= 8);
            QVERIFY(trailing < 12);
            QVERIFY(qAbs(leading - trailing) <= 4);
            for (int i = 1; i < runs.size(); ++i)
                QVERIFY2(runs[i].first - runs[i - 1].second - 1 >= 8,
                         "Adjacent cards must retain their compact 8 px gap.");

            const QSize stable = grid.gridSize();
            const QSize viewport = grid.viewport()->size();
            QTest::qWait(35);
            QCOMPARE(grid.gridSize(), stable);
            QCOMPARE(grid.viewport()->size(), viewport);
            QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
        }

        grid.resize(1003, 480);
        QTRY_COMPARE_WITH_TIMEOUT(grid.horizontalScrollBar()->maximum(), 0, 3000);
        QTest::qWait(40);
        const int columns = qMax(1, grid.viewport()->width() / grid.gridSize().width());
        QVERIFY(columns > 1);
        grid.setCurrentIndex(model.index(0, 0));
        grid.setFocus();
        QTest::keyClick(&grid, Qt::Key_Right);
        QCOMPARE(grid.currentIndex().row(), 1);
        QTest::keyClick(&grid, Qt::Key_Down);
        QCOMPARE(grid.currentIndex().row(), 1 + columns);
        QSignalSpy activation(&grid, &QAbstractItemView::activated);
        QTest::keyClick(&grid, Qt::Key_Return);
        QCOMPARE(activation.size(), 1);
        QCOMPARE(activation.first().first().value<QModelIndex>().row(), 1 + columns);
    }

    void emptyAndSingleRemainCompact()
    {
        QStandardItemModel model;
        AssetGrid grid;
        grid.setModel(&model);
        grid.setCardSize(176);
        grid.resize(1003, 480);
        grid.show();
        QTest::qWait(40);
        QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
        const QSize emptyCell = grid.gridSize();
        QVERIFY(emptyCell.width() > 0 && emptyCell.width() < 300);
        fillModel(model, 1);
        QTRY_VERIFY_WITH_TIMEOUT(grid.visualRect(model.index(0, 0)).isValid(), 3000);
        QTest::qWait(40);
        QCOMPARE(grid.gridSize(), emptyCell);
        const QRect only = grid.visualRect(model.index(0, 0));
        QVERIFY(only.width() < 300);
        QCOMPARE(grid.indexAt(only.center()).row(), 0);
        QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
        QCOMPARE(grid.verticalScrollBar()->maximum(), 0);
        for (int width : {420, 1280, 750, 1003}) {
            grid.resize(width, 480);
            QTest::qWait(35);
            QVERIFY(grid.visualRect(model.index(0, 0)).width() < 300);
            QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
        }
        model.clear();
        QTest::qWait(35);
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
    }

    void scrollbarThresholdSettles()
    {
        QStandardItemModel model;
        fillModel(model, 12);
        AssetGrid grid;
        grid.setModel(&model);
        grid.setCardSize(176);
        ResizeCounter counter;
        grid.viewport()->installEventFilter(&counter);
        grid.show();
        for (int width : {750, 1003, 420}) {
            grid.resize(width, 480);
            QTest::qWait(40);
            const int columns = qMax(1, grid.viewport()->width() / grid.gridSize().width());
            const int rows = (model.rowCount() + columns - 1) / columns;
            const int boundary = rows * grid.gridSize().height() + grid.height() - grid.viewport()->height();
            for (int delta : {-8, -1, 0, 1, 8}) {
                const int before = counter.count;
                grid.resize(width, boundary + delta);
                // Give each queued resize/layout chain several event turns. A
                // second equal observation period must see no further relayout.
                for (int i = 0; i < 30; ++i) QTest::qWait(1);
                const int settled = counter.count;
                const QSize cell = grid.gridSize();
                const QSize viewport = grid.viewport()->size();
                const int scrollMaximum = grid.verticalScrollBar()->maximum();
                QVERIFY2(settled - before <= 32, "Scrollbar settlement caused an unbounded viewport resize chain.");
                for (int i = 0; i < 30; ++i) QTest::qWait(1);
                QCOMPARE(counter.count, settled);
                QCOMPARE(grid.gridSize(), cell);
                QCOMPARE(grid.viewport()->size(), viewport);
                QCOMPARE(grid.verticalScrollBar()->maximum(), scrollMaximum);
                QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);
            }
        }
    }

    void previewContainment_data()
    {
        QTest::addColumn<QString>("shape");
        for (const QString &shape : {QString("square"), QString("wide"), QString("tall"), QString("alpha")})
            QTest::newRow(qPrintable(shape)) << shape;
    }

    void previewContainment()
    {
        QFETCH(QString, shape);
        applyUiTheme("mint");
        const auto square = paintedCard("square");
        const QRect area = coloredBounds(square, {previewInk});
        QVERIFY(area.isValid());
        QVERIFY(area.left() <= 5 && area.top() <= 5);
        QVERIFY(area.right() >= square.width() - 6);
        QCOMPARE(area.width(), area.height());
        QVERIFY2(!near(square.pixelColor(area.topLeft()), previewInk), "The preview lost its rounded top corner.");
        QVERIFY(square.pixelColor(area.topLeft()).alpha() < 200);
        QVERIFY(near(square.pixelColor(QPoint(area.left(), area.center().y())), previewInk));
        QVERIFY(near(square.pixelColor(QPoint(area.right(), area.center().y())), previewInk));

        const auto image = paintedCard(shape);
        const QRect bounds = coloredBounds(image, previewColors);
        if (shape == "wide" || shape == "tall") {
            // All four source corners remain present, so fitting cannot silently crop.
            for (int i = 1; i < previewColors.size(); ++i)
                QVERIFY(!coloredBounds(image, {previewColors[i]}).isEmpty());
            QVERIFY(area.contains(bounds));
            if (shape == "wide") {
                QVERIFY(qAbs(bounds.width() - 2 * bounds.height()) <= 2);
                QVERIFY(qAbs(bounds.width() - area.width()) <= 2);
            } else {
                QVERIFY(qAbs(bounds.height() - 2 * bounds.width()) <= 2);
                QVERIFY(qAbs(bounds.height() - area.height()) <= 2);
            }
        } else if (shape == "alpha") {
            QVERIFY(area.contains(bounds));
            QVERIFY(bounds.width() < area.width() / 2);
            const QColor background = image.pixelColor(30, 40);
            QVERIFY(background.alpha() < 200);
            for (int y : {40, 48, 56, 64})
                for (int x : {30, 38, 46, 54})
                    QCOMPARE(image.pixelColor(x, y), background);
            QVERIFY2(near(image.pixelColor(area.center()), previewInk), "The transparent preview's visible content disappeared.");
        } else {
            QCOMPARE(bounds, area);
        }
    }

    void fourThemeContactSheet()
    {
        const QString output = qEnvironmentVariable("IMAGIDOCK_GRID_ARTIFACT_DIR");
        if (output.isEmpty()) return;
        const QString project = QDir::cleanPath(QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(".."));
        const QString directory = QDir::cleanPath(QFileInfo(output).absoluteFilePath());
        QVERIFY2(directory.startsWith(project + "/", Qt::CaseInsensitive), "Artifacts must stay inside the project directory.");
        QVERIFY(QDir().mkpath(directory));
        QImage contact(QSize(1056, 1432), QImage::Format_ARGB32_Premultiplied);
        contact.fill(Qt::transparent);
        QPainter painter(&contact);
        int row = 0;
        for (const QString &theme : {QString("ivory"), QString("graphite"), QString("ocean"), QString("mint")}) {
            applyUiTheme(theme);
            const auto colors = themeColors();
            const int top = row++ * 358;
            painter.fillRect(0, top, contact.width(), 358, colors.top);
            painter.setPen(colors.text);
            painter.setFont(QApplication::font());
            painter.drawText(QRect(12, top + 4, 1032, 24), Qt::AlignLeft | Qt::AlignVCenter,
                             colors.name + QStringLiteral(" — square / wide / tall / alpha"));
            int column = 0;
            for (const QString &shape : {QString("square"), QString("wide"), QString("tall"), QString("alpha")})
                painter.drawImage(QPoint(4 + column++ * 264, top + 30), paintedCard(shape));
            QStandardItemModel model;
            fillModel(model, 12);
            const QStringList shapes{"square", "wide", "tall", "alpha"};
            for (int index = 0; index < model.rowCount(); ++index)
                model.item(index)->setData(preview(shapes[index % shapes.size()]), Qt::DecorationRole);
            AssetGrid grid;
            grid.setModel(&model);
            grid.resize(1003, 880);
            grid.show();
            QTest::qWait(40);
            QImage viewport(grid.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
            viewport.fill(colors.top);
            grid.viewport()->render(&viewport);
            QVERIFY(viewport.save(directory + QString("/grid-%1-viewport-offscreen.png").arg(theme)));
        }
        painter.end();
        QVERIFY(contact.save(directory + "/grid-four-themes-offscreen.png"));
    }
};

QTEST_MAIN(GridTests)
#include "grid_tests.moc"
