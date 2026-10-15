// ImagiDock SAI 1 thumbnail reader. libsai is MIT; see adapter notices.
#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <filesystem>
#include <sai.hpp>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() != 3) throw std::runtime_error("input and output paths required");
        const auto input = std::filesystem::path(args[1].toStdWString());
        const auto output = std::filesystem::path(args[2].toStdWString());
        if (std::filesystem::absolute(input).lexically_normal() == std::filesystem::absolute(output).lexically_normal())
            throw std::runtime_error("output must differ from source");
        if (!std::filesystem::is_regular_file(input)) throw std::runtime_error("input file not found");
        sai::Document document(input);
        if (!document.IsOpen()) throw std::runtime_error("cannot read SAI document");
        auto entry = document.GetEntry("thumbnail");
        if (!entry) throw std::runtime_error("no saved thumbnail; link a PNG/PSD preview");
        std::uint32_t width = 0, height = 0, magic = 0;
        if (entry->Read(width) != 4 || entry->Read(height) != 4 || entry->Read(magic) != 4)
            throw std::runtime_error("truncated thumbnail header");
        if (magic != sai::Tag<std::endian::little>("BM32") || !width || !height || width > 8192 || height > 8192 || std::uint64_t(width) * height > 16000000)
            throw std::runtime_error("invalid or oversized saved thumbnail");
        const std::size_t bytes = std::size_t(width) * height * 4;
        if (entry->GetSize() < 12 || bytes > entry->GetSize() - 12) throw std::runtime_error("truncated thumbnail pixels");
        QImage image(int(width), int(height), QImage::Format_ARGB32);
        if (image.isNull()) throw std::runtime_error("thumbnail allocation failed");
        if (entry->Read({reinterpret_cast<std::byte*>(image.bits()), bytes}) != bytes)
            throw std::runtime_error("truncated thumbnail stream");
        if (!image.save(args[2], "PNG")) throw std::runtime_error("cannot save thumbnail");
        auto [canvasWidth, canvasHeight] = document.GetCanvasSize();
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"width", int(width)}, {"height", int(height)},
            {"canvas_width", int(canvasWidth)}, {"canvas_height", int(canvasHeight)}}).toJson(QJsonDocument::Compact) << '\n';
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
