#include "speechrecogniser.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <cmath>

SpeechRecogniser::SpeechRecogniser(const QString& id,
                                   const QString& secret,
                                   const QString& device_id,
                                   QObject* parent)
    : QObject(parent)
    , config(ConfigManager::instance())
    , networkManager(new QNetworkAccessManager(this))
    , client_id(id)
    , client_secret(secret)
    , cuid(device_id)
{
}

SpeechRecogniser::~SpeechRecogniser(){}

void SpeechRecogniser::setApiKeys(const QString& id, const QString& secret)
{
    client_id = id;
    client_secret = secret;
}

void SpeechRecogniser::setCuid(const QString& device_id)
{
    cuid = device_id;
}

QString SpeechRecogniser::getAccessToken()
{
    // 先检查config中是否有token
    if (!config.baiDuToken.isEmpty()) {
        qDebug() << "Using token from config:" << config.baiDuToken;
        access_token = config.baiDuToken;
        return access_token;
    }

    qDebug() << "No token in config, requesting new token...";

    QUrl url("https://aip.baidubce.com/oauth/2.0/token");
    QByteArray postData = buildAccessTokenRequest();

    QByteArray response = httpPost(url, postData, "application/x-www-form-urlencoded");

    if (response.isEmpty()) {
        qDebug() << "Access Token API Response: EMPTY RESPONSE";
        return "";
    }

    qDebug() << "Access Token API Response:" << QString::fromUtf8(response);

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) {
        qDebug() << "Failed to parse access token response JSON";
        return "";
    }

    QJsonObject obj = doc.object();

    if (obj.contains("access_token")) {
        access_token = obj["access_token"].toString();
        qDebug() << "New access token received:" << access_token;

        // 保存到config中供下次使用
        config.baiDuToken = access_token;
        // config.saveConfig();                      // TODO
        qDebug() << "Token saved to config";

        return access_token;
    } else {
        QString error = obj["error_description"].toString("Unknown error");
        qDebug() << "Failed to get access token:" << error;
        return "";
    }
}

QString SpeechRecogniser::recognizeSpeech(const QVector<float>& audio_data, int sample_rate, int channels)
{
    if (!validateAudioData(audio_data)) {
        return "Error: Invalid audio data";
    }

    // 获取access token：先在配置缓存中查找，没有的话申请一个并保存
    if (access_token.isEmpty()) {
        access_token = getAccessToken();
    }

    if (access_token.isEmpty()) {
        return "Error: Access token is not available. Please get access token first.";
    }

    QUrl url("https://vop.baidu.com/server_api");
    QByteArray postData = buildRecognitionRequest(audio_data, sample_rate, channels);

    QByteArray response = httpPost(url, postData, "application/json");

    if (response.isEmpty()) {
        return "Error: Network request failed";
    }

    return parseRecognitionResult(QString::fromUtf8(response));
}

bool SpeechRecogniser::validateAudioData(const QVector<float>& audio_data)
{
    if (audio_data.isEmpty()) {
        qDebug() << "Audio data is empty";
        return false;
    }

    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::lowest();
    float sum = 0.0f;

    for (const float& sample : audio_data) {
        if (sample < min_val) min_val = sample;
        if (sample > max_val) max_val = sample;
        sum += std::abs(sample);
    }

    float average = sum / audio_data.size();

    qDebug() << "Audio Data Validation:";
    qDebug() << "  Samples:" << audio_data.size();
    qDebug() << "  Duration:" << (audio_data.size() / 16000.0) << "seconds";
    qDebug() << "  Range: min=" << min_val << "max=" << max_val;
    qDebug() << "  Average amplitude:" << average;

    if (max_val > 10.0f || min_val < -10.0f) {
        qDebug() << "Warning: Audio data range seems unusual for normalized audio";
    }

    // 检查是否为静音（平均幅度很小识别错误率指数上涨）
    if (average < 0.001f) {
        qDebug() << "Warning: Audio data appears to be very quiet or silent";
    }

    return true;
}

QVector<int16_t> SpeechRecogniser::convertFloatToInt16(const QVector<float>& audio_data)
{
    QVector<int16_t> int16_data;
    int16_data.resize(audio_data.size());

    // 找到数据的最大绝对值来进行归一化
    float max_amplitude = 0.0f;
    for (const float& sample : audio_data) {
        float abs_val = std::abs(sample);
        if (abs_val > max_amplitude) {
            max_amplitude = abs_val;
        }
    }

    // 如果最大幅度为0，直接返回全0
    if (max_amplitude == 0.0f) {
        qDebug() << "Warning: All audio samples are zero";
        return int16_data;
    }

    // 归一化因子，确保数据在-1.0到1.0范围内
    float normalization_factor = 1.0f / max_amplitude;

    qDebug() << "Audio Conversion:";
    qDebug() << "  Max amplitude:" << max_amplitude;
    qDebug() << "  Normalization factor:" << normalization_factor;

    for (int i = 0; i < audio_data.size(); ++i) {
        // 归一化并转换为int16
        float normalized_sample = audio_data[i] * normalization_factor;
        int16_data[i] = static_cast<int16_t>(normalized_sample * 32767.0f);

        // 限制在int16范围内
        if (int16_data[i] > 32767) int16_data[i] = 32767;
        if (int16_data[i] < -32768) int16_data[i] = -32768;
    }

    return int16_data;
}

QByteArray SpeechRecogniser::audioToBase64(const QVector<float>& audio_data)
{
    QVector<int16_t> int16_data = convertFloatToInt16(audio_data);

    QByteArray byteArray(reinterpret_cast<const char*>(int16_data.constData()),
                         int16_data.size() * sizeof(int16_t));

    QByteArray base64_data = byteArray.toBase64();

    qDebug() << "Audio to Base64 Conversion:";
    qDebug() << "  Input float samples:" << audio_data.size();
    qDebug() << "  Output int16 samples:" << int16_data.size();
    qDebug() << "  Byte array size:" << byteArray.size();
    qDebug() << "  Base64 length:" << base64_data.length();

    return base64_data;
}

QByteArray SpeechRecogniser::buildRecognitionRequest(const QVector<float>& audio_data, int sample_rate, int channels)
{
    QString audio_base64 = audioToBase64(audio_data);

    QJsonObject request_obj;
    request_obj["format"] = "pcm";
    request_obj["rate"] = sample_rate;
    request_obj["channel"] = channels;
    request_obj["cuid"] = cuid;
    request_obj["token"] = access_token;
    request_obj["speech"] = audio_base64;
    request_obj["len"] = static_cast<int>(audio_data.size() * sizeof(int16_t)); // 修正为int16_t的大小

    QJsonDocument doc(request_obj);
    QByteArray json_data = doc.toJson();

    return json_data;
}

QByteArray SpeechRecogniser::httpPost(const QUrl& url, const QByteArray& data, const QString& contentType)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");

    // 连接复用
    request.setRawHeader("Connection", "Keep-Alive");
    request.setRawHeader("Keep-Alive", "timeout=30, max=100");

    qDebug() << "HTTP POST Request URL:" << url.toString();
    qDebug() << "HTTP POST Content Type:" << contentType;
    qDebug() << "HTTP POST Data Size:" << data.size();

    QNetworkReply* reply = networkManager->post(request, data);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(15000);
    loop.exec();

    QByteArray response;
    if (timer.isActive()) {
        timer.stop();
        if (reply->error() == QNetworkReply::NoError) {
            response = reply->readAll();
            qDebug() << "HTTP POST Request Successful";
            qDebug() << "Response size:" << response.size();
        } else {
            qDebug() << "HTTP POST Network Error:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        }
    } else {
        qDebug() << "HTTP POST Request Timeout";
        reply->abort();
    }

    reply->deleteLater();
    return response;
}

QByteArray SpeechRecogniser::buildAccessTokenRequest()
{
    QString data = QString("grant_type=client_credentials&client_id=%1&client_secret=%2")
    .arg(client_id)
        .arg(client_secret);
    qDebug() << "Access Token Request Parameters:" << data;
    return data.toUtf8();
}

QString SpeechRecogniser::parseRecognitionResult(const QString& json_result)
{
    qDebug() << "Parsing Recognition Result...";

    QJsonDocument doc = QJsonDocument::fromJson(json_result.toUtf8());
    if (doc.isNull()) {
        qDebug() << "Parse Error: Invalid JSON response";
        return "Error: Invalid JSON response";
    }

    QJsonObject obj = doc.object();

    qDebug() << "All Response Fields:";
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        qDebug() << "  " << it.key() << ":" << it.value().toVariant().toString();
    }

    if (obj.contains("err_no")) {
        int error_code = obj["err_no"].toInt();
        QString error_msg = obj["err_msg"].toString("Unknown error");
        qDebug() << "API Error Code:" << error_code << "Message:" << error_msg;

        if (error_code != 0) {
            return QString("Error: %1 (code: %2)").arg(error_msg).arg(error_code);
        }
    }

    if (obj.contains("result") && obj["result"].isArray()) {
        QJsonArray result_array = obj["result"].toArray();

        if (!result_array.isEmpty()) {
            QString final_result = result_array[0].toString();
            qDebug() << "Recognition Result:" << final_result;
            return final_result;
        } else {
            qDebug() << "Parse Warning: Empty recognition result array";
            return "Error: Empty recognition result";
        }
    } else {
        qDebug() << "Parse Warning: No result field found in response";
    }

    return "Debug: " + json_result;
}
