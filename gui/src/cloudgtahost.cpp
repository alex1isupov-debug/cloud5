// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <cloudgtahost.h>

#include <qmlmainwindow.h>
#include <settings.h>
#include <streamsession.h>

#include <chiaki/session.h>

#include <QByteArray>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <cstring>

namespace
{
constexpr qsizetype LaunchFrameLimit = 16 * 1024;
constexpr qsizetype CredentialFrameLimit = 4096;
constexpr char ProtocolMagic[] = "CGTARP1\n";
constexpr unsigned char CredentialVersion = 1;
constexpr qsizetype CredentialDecodedSize = 1 + CHIAKI_SESSION_AUTH_SIZE + 0x10;

struct ManagedLaunch
{
	QString console_target;
	QString decoder;
	int bitrate_kbps = 0;
};

void SecureZero(QByteArray &value)
{
	if(!value.isEmpty())
	{
		volatile char *cursor = value.data();
		for(qsizetype i = 0; i < value.size(); ++i)
			cursor[i] = 0;
	}
	value.clear();
}

void WriteEvent(const char *event, const char *error_code = nullptr)
{
	if(error_code)
		std::fprintf(stdout, "{\"event\":\"%s\",\"error_code\":\"%s\"}\n", event, error_code);
	else
		std::fprintf(stdout, "{\"event\":\"%s\"}\n", event);
	std::fflush(stdout);
}

bool HasExactKeys(const QJsonObject &object, const QSet<QString> &allowed)
{
	if(object.size() != allowed.size())
		return false;
	for(auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
	{
		if(!allowed.contains(iterator.key()))
			return false;
	}
	return true;
}

bool IsInteger(const QJsonValue &value, int expected)
{
	return value.isDouble() && value.toDouble() == expected;
}

bool IsSafeIpv4(const QString &value)
{
	const auto parts = value.split('.');
	if(parts.size() != 4)
		return false;
	for(const auto &part : parts)
	{
		if(part.isEmpty() || (part.size() > 1 && part.startsWith('0')))
			return false;
		bool ok = false;
		const int number = part.toInt(&ok);
		if(!ok || number < 0 || number > 255)
			return false;
	}
	return value.startsWith(QStringLiteral("10."))
		|| value.startsWith(QStringLiteral("192.168."))
		|| (parts[0] == QStringLiteral("172") && parts[1].toInt() >= 16 && parts[1].toInt() <= 31);
}

bool ParseLaunch(const QByteArray &frame, ManagedLaunch &launch)
{
	QJsonParseError parse_error;
	const auto document = QJsonDocument::fromJson(frame, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
		return false;
	const auto root = document.object();
	const QSet<QString> root_keys = {
		QStringLiteral("contract_version"), QStringLiteral("session_id"),
		QStringLiteral("profile_slot_id"), QStringLiteral("console_target"), QStringLiteral("stream")
	};
	if(!HasExactKeys(root, root_keys) || !IsInteger(root.value(QStringLiteral("contract_version")), 1))
		return false;
	const auto session_id = root.value(QStringLiteral("session_id"));
	if(!session_id.isDouble() || session_id.toDouble() < 1 || session_id.toDouble() > 9007199254740991.0
		|| session_id.toDouble() != static_cast<qint64>(session_id.toDouble()))
		return false;
	const auto profile_slot_id = root.value(QStringLiteral("profile_slot_id"));
	if(!profile_slot_id.isString() || QUuid(profile_slot_id.toString()).isNull())
		return false;
	const auto console_target = root.value(QStringLiteral("console_target"));
	if(!console_target.isString() || !IsSafeIpv4(console_target.toString()))
		return false;
	const auto stream_value = root.value(QStringLiteral("stream"));
	if(!stream_value.isObject())
		return false;
	const auto stream = stream_value.toObject();
	const QSet<QString> stream_keys = {
		QStringLiteral("profile_id"), QStringLiteral("codec"), QStringLiteral("width"),
		QStringLiteral("height"), QStringLiteral("fps"), QStringLiteral("bitrate_kbps"),
		QStringLiteral("renderer"), QStringLiteral("decoder")
	};
	if(!HasExactKeys(stream, stream_keys)
		|| stream.value(QStringLiteral("profile_id")).toString() != QStringLiteral("pilot-safe-h264-v1")
		|| stream.value(QStringLiteral("codec")).toString() != QStringLiteral("h264")
		|| !IsInteger(stream.value(QStringLiteral("width")), 1280)
		|| !IsInteger(stream.value(QStringLiteral("height")), 720)
		|| !IsInteger(stream.value(QStringLiteral("fps")), 60)
		|| stream.value(QStringLiteral("renderer")).toString() != QStringLiteral("opengl"))
		return false;
	const auto bitrate = stream.value(QStringLiteral("bitrate_kbps"));
	if(!bitrate.isDouble() || bitrate.toDouble() < 2000 || bitrate.toDouble() > 15000
		|| bitrate.toDouble() != static_cast<int>(bitrate.toDouble()))
		return false;
	const auto decoder = stream.value(QStringLiteral("decoder"));
	const QSet<QString> decoders = {
		QStringLiteral("auto"), QStringLiteral("d3d11va"), QStringLiteral("cuda"), QStringLiteral("none")
	};
	if(!decoder.isString() || !decoders.contains(decoder.toString()))
		return false;
	launch.console_target = console_target.toString();
	launch.decoder = decoder.toString();
	launch.bitrate_kbps = static_cast<int>(bitrate.toDouble());
	return true;
}

bool ReadExactly(QFile &input, QByteArray &output, qsizetype length)
{
	output.clear();
	while(output.size() < length)
	{
		const auto chunk = input.read(length - output.size());
		if(chunk.isEmpty())
			return false;
		output.append(chunk);
	}
	return true;
}

bool ReadFrame(QFile &input, QByteArray &output, qsizetype limit)
{
	QByteArray length_bytes;
	if(!ReadExactly(input, length_bytes, 4))
		return false;
	const auto *bytes = reinterpret_cast<const unsigned char *>(length_bytes.constData());
	const quint32 length = (static_cast<quint32>(bytes[0]) << 24)
		| (static_cast<quint32>(bytes[1]) << 16)
		| (static_cast<quint32>(bytes[2]) << 8)
		| static_cast<quint32>(bytes[3]);
	if(length == 0 || length > static_cast<quint32>(limit))
		return false;
	return ReadExactly(input, output, static_cast<qsizetype>(length));
}

bool DecodeCredential(QByteArray &frame, QByteArray &regist_key, QByteArray &morning)
{
	QByteArray decoded = QByteArray::fromBase64(frame, QByteArray::AbortOnBase64DecodingErrors);
	SecureZero(frame);
	if(decoded.size() != CredentialDecodedSize
		|| static_cast<unsigned char>(decoded[0]) != CredentialVersion)
	{
		SecureZero(decoded);
		return false;
	}
	regist_key = decoded.mid(1, CHIAKI_SESSION_AUTH_SIZE);
	morning = decoded.mid(1 + CHIAKI_SESSION_AUTH_SIZE, 0x10);
	SecureZero(decoded);
	return true;
}

QByteArray BuildSelfTestLaunch()
{
	return QByteArrayLiteral(
		"{\"contract_version\":1,\"session_id\":1,\"profile_slot_id\":\"123e4567-e89b-12d3-a456-426614174000\","
		"\"console_target\":\"192.168.1.9\",\"stream\":{\"profile_id\":\"pilot-safe-h264-v1\","
		"\"codec\":\"h264\",\"width\":1280,\"height\":720,\"fps\":60,\"bitrate_kbps\":6000,"
		"\"renderer\":\"opengl\",\"decoder\":\"auto\"}}");
}
}

int CloudGTAHostFail(const QString &code)
{
	const auto safe = code.toLatin1();
	WriteEvent("failed", safe.constData());
	return 2;
}

int CloudGTAHostSelfTest()
{
	ManagedLaunch launch;
	if(!ParseLaunch(BuildSelfTestLaunch(), launch) || launch.console_target != QStringLiteral("192.168.1.9"))
		return CloudGTAHostFail(QStringLiteral("self_test_launch_failed"));
	auto unknown = BuildSelfTestLaunch();
	unknown.replace("\"contract_version\":1", "\"contract_version\":1,\"unknown\":true");
	if(ParseLaunch(unknown, launch))
		return CloudGTAHostFail(QStringLiteral("self_test_unknown_field_failed"));
	QByteArray credential(CredentialDecodedSize, 0x2a);
	credential[0] = static_cast<char>(CredentialVersion);
	auto encoded = credential.toBase64();
	SecureZero(credential);
	QByteArray regist_key;
	QByteArray morning;
	if(!DecodeCredential(encoded, regist_key, morning)
		|| regist_key.size() != CHIAKI_SESSION_AUTH_SIZE || morning.size() != 0x10)
		return CloudGTAHostFail(QStringLiteral("self_test_credential_failed"));
	SecureZero(regist_key);
	SecureZero(morning);
	WriteEvent("stopped");
	return 0;
}

int RunCloudGTAHost(QGuiApplication &app, Settings *settings)
{
	QFile input;
	if(!input.open(stdin, QIODevice::ReadOnly))
		return CloudGTAHostFail(QStringLiteral("protocol_input_unavailable"));
	QByteArray magic;
	QByteArray launch_frame;
	QByteArray credential_frame;
	if(!ReadExactly(input, magic, static_cast<qsizetype>(std::strlen(ProtocolMagic)))
		|| magic != QByteArray(ProtocolMagic, static_cast<qsizetype>(std::strlen(ProtocolMagic)))
		|| !ReadFrame(input, launch_frame, LaunchFrameLimit)
		|| !ReadFrame(input, credential_frame, CredentialFrameLimit)
		|| !input.read(1).isEmpty())
	{
		SecureZero(credential_frame);
		return CloudGTAHostFail(QStringLiteral("protocol_frame_invalid"));
	}

	ManagedLaunch launch;
	QByteArray regist_key;
	QByteArray morning;
	if(!ParseLaunch(launch_frame, launch) || !DecodeCredential(credential_frame, regist_key, morning))
	{
		SecureZero(regist_key);
		SecureZero(morning);
		return CloudGTAHostFail(QStringLiteral("protocol_payload_invalid"));
	}

	StreamSessionConnectInfo connect_info(
		settings,
		CHIAKI_TARGET_PS5_1,
		launch.console_target,
		QString(),
		std::move(regist_key),
		std::move(morning),
		QString(),
		QString(),
		false,
		true,
		false,
		false);
	connect_info.render_backend = RenderBackend::OpenGL;
	connect_info.decoder = Decoder::Ffmpeg;
	connect_info.hw_decoder = launch.decoder;
	connect_info.log_level_mask = CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE;
	connect_info.log_sanitize = true;
	connect_info.log_file.clear();
	chiaki_connect_video_profile_preset(
		&connect_info.video_profile,
		CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
		CHIAKI_VIDEO_FPS_PRESET_60);
	connect_info.video_profile.bitrate = static_cast<unsigned int>(launch.bitrate_kbps);
	connect_info.video_profile.codec = CHIAKI_CODEC_H264;

	QmlMainWindow main_window(connect_info);
	SecureZero(connect_info.regist_key);
	SecureZero(connect_info.morning);
	bool first_frame = false;
	bool terminal_event = false;
	QObject::connect(&main_window, &QmlMainWindow::hasVideoChanged, &app, [&]() {
		if(main_window.hasVideo() && !first_frame)
		{
			first_frame = true;
			WriteEvent("first_frame");
		}
	});
	if(auto *session = main_window.getStreamSession())
	{
		QObject::connect(session, &StreamSession::ConnectedChanged, &app, [&, session]() {
			if(first_frame && !session->GetConnected() && !terminal_event)
				WriteEvent("reconnecting");
		});
		QObject::connect(session, &StreamSession::SessionQuit, &app, [&]() {
			if(terminal_event)
				return;
			terminal_event = true;
			if(first_frame)
				WriteEvent("stopped");
			else
				WriteEvent("failed", "remote_play_connect_failed");
		});
	}
	QTimer heartbeat;
	heartbeat.setInterval(10000);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]() {
		if(first_frame && !terminal_event)
			WriteEvent("heartbeat");
	});
	heartbeat.start();
	main_window.show();
	const int result = app.exec();
	if(!terminal_event)
		WriteEvent("stopped");
	return result;
}
