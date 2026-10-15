// Headless media preview using the application's Qt Multimedia backend.
#include <QGuiApplication>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QPainter>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

struct Peak { float low = 0; float high = 0; };

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 5) {
        qCritical("Usage: imagidock-media <audio|video> <input> <output.png> <size>");
        return 1;
    }
    const QString kind = args[1], input = args[2], output = args[3];
    const int size = args[4].toInt();
    if ((kind != "audio" && kind != "video") || size < 64 || size > 2048 || !QFileInfo(input).isFile()
        || QFileInfo(input).absoluteFilePath() == QFileInfo(output).absoluteFilePath()) return 1;
    bool done = false;
    auto finish = [&](bool ok, QJsonObject info) {
        if (done) return;
        done = true;
        info.insert("ok", ok);
        fprintf(stdout, "%s\n", QJsonDocument(info).toJson(QJsonDocument::Compact).constData());
        fflush(stdout);
        QTimer::singleShot(0, &app, [&, ok] { app.exit(ok ? 0 : 1); });
    };
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] { finish(false, {{"error", "Media decoding timed out"}}); });
    watchdog.start(110000);
    QAudioDecoder audio;
    QMediaPlayer video;
    QVideoSink sink;
    std::vector<Peak> peaks;
    Peak pending;
    qint64 samples = 0, blockSamples = 0, totalFrames = 0;
    int sampleRate = 0;
    constexpr qint64 BlockFrames = 1024;
    auto saveWaveform = [&] {
        if (done) return;
        if (blockSamples) peaks.push_back(pending);
        if (peaks.empty()) { finish(false, {{"error", "No decodable audio samples"}}); return; }
        QImage image(size, std::max(64, size / 2), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(23, 29, 41));
        QPainter painter(&image);
        const int middle = image.height() / 2;
        painter.setPen(QColor(54, 67, 86));
        painter.drawLine(0, middle, size, middle);
        painter.setPen(QColor(105, 191, 223));
        for (int x = 0; x < size; ++x) {
            const auto begin = std::size_t(x) * peaks.size() / size;
            const auto end = std::min(peaks.size(), std::max(begin + 1, std::size_t(x + 1) * peaks.size() / size));
            Peak value;
            for (auto i = begin; i < end; ++i) {
                value.low = std::min(value.low, peaks[i].low);
                value.high = std::max(value.high, peaks[i].high);
            }
            painter.drawLine(x, middle - int(value.high * (middle - 5)), x, middle - int(value.low * (middle - 5)));
        }
        painter.end();
        const bool saved = image.save(output, "PNG");
        finish(saved, {{"preview_kind", "waveform"}, {"width", image.width()}, {"height", image.height()},
            {"metadata", QJsonObject{{"decoder", "Qt Multimedia"}, {"sample_rate", sampleRate},
                {"waveform_seconds", sampleRate ? double(totalFrames) / sampleRate : 0.0},
                {"waveform_truncated", totalFrames >= qint64(sampleRate) * 7200}}}});
    };
    if (kind == "audio") {
        QObject::connect(&audio, &QAudioDecoder::bufferReady, &app, [&] {
            if (done) return;
            const auto buffer = audio.read();
            const auto format = buffer.format();
            sampleRate = format.sampleRate();
            const int channels = format.channelCount();
            if (!channels || sampleRate <= 0) { finish(false, {{"error", "Invalid audio format"}}); return; }
            const int frames = buffer.frameCount();
            for (int frame = 0; frame < frames; ++frame) {
                for (int channel = 0; channel < channels; ++channel) {
                    const int index = frame * channels + channel;
                    float value = 0;
                    switch (format.sampleFormat()) {
                    case QAudioFormat::UInt8: value = (int(buffer.constData<quint8>()[index]) - 128) / 128.0f; break;
                    case QAudioFormat::Int16: value = buffer.constData<qint16>()[index] / 32768.0f; break;
                    case QAudioFormat::Int32: value = float(buffer.constData<qint32>()[index] / 2147483648.0); break;
                    case QAudioFormat::Float: value = buffer.constData<float>()[index]; break;
                    default: finish(false, {{"error", "Unsupported PCM format"}}); return;
                    }
                    if (std::isfinite(value)) {
                        pending.low = std::min(pending.low, std::clamp(value, -1.0f, 1.0f));
                        pending.high = std::max(pending.high, std::clamp(value, -1.0f, 1.0f));
                    }
                }
                ++totalFrames;
                if (++blockSamples == BlockFrames) { peaks.push_back(pending); pending = {}; blockSamples = 0; }
                if (totalFrames >= qint64(sampleRate) * 7200) { audio.stop(); saveWaveform(); return; }
            }
        });
        QObject::connect(&audio, &QAudioDecoder::finished, &app, saveWaveform);
        QObject::connect(&audio, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &app,
                         [&](QAudioDecoder::Error) { finish(false, {{"error", audio.errorString()}}); });
        audio.setSource(QUrl::fromLocalFile(input));
        audio.start();
    } else {
        video.setVideoOutput(&sink);
        QObject::connect(&sink, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame& frame) {
            if (done || !frame.isValid()) return;
            const auto original = frame.toImage();
            if (original.isNull()) return;
            const auto image = original.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const bool saved = image.save(output, "PNG");
            finish(saved, {{"preview_kind", "video_frame"}, {"width", image.width()}, {"height", image.height()},
                {"canvas_width", original.width()}, {"canvas_height", original.height()},
                {"metadata", QJsonObject{{"decoder", "Qt Multimedia"}, {"frame", "first_decodable"},
                    {"duration_ms", double(video.duration())}}}});
            video.stop();
        });
        QObject::connect(&video, &QMediaPlayer::errorOccurred, &app,
                         [&](QMediaPlayer::Error, const QString& message) { finish(false, {{"error", message}}); });
        QObject::connect(&video, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia && !done) finish(false, {{"error", "No decodable video frame"}});
        });
        video.setSource(QUrl::fromLocalFile(input));
        video.play();
    }
    return app.exec();
}
