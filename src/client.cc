/*! \file client.cc
 *
 * Class representing remote clients to the BLDS.
 *
 * (C) 2017 Benjamin Naecker bnaecker@stanford.edu
 */

#include "client.h"

#include "libdata-source/include/configuration.h"
#include "libdata-source/include/data-source.h" // for (de)serialization methods

#include <cstring> // std::memcpy
#include <algorithm> // std::count_if

Client::Client(QTcpSocket* sock, QObject* parent) :
	QObject(parent),
	m_socket(sock),
	m_stream(sock),
	m_requestedAllData(false)
{
	m_socket->setParent(this);
	m_stream.setByteOrder(QDataStream::LittleEndian);
	m_stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
	QObject::connect(m_socket, &QTcpSocket::readyRead,
			this, &Client::handleReadyRead);
	QObject::connect(m_socket, &QTcpSocket::disconnected,
			this, [this]() -> void { emit disconnected(this); });
}

Client::~Client()
{
}

void Client::handleReadyRead()
{
	do {
		quint32 size = 0;
		if (m_socket->peek(reinterpret_cast<char*>(&size), sizeof(size)) <
				static_cast<qint64>(sizeof(size))) {
			// size is not yet available
			return;
		}

		/* Check if full message available */
		if ( m_socket->bytesAvailable() < static_cast<qint64>((size - sizeof(size))) ) {
			return;
		} 

		/* Read message */
		m_socket->read(sizeof(size)); // ignore size, already read
		handleMessage(size);

	} while (true);
}

void Client::handleMessage(quint32 size)
{
	/* Determine message type. */
	auto type = m_socket->readLine();
	if (type.size() == 0) {
		emit messageError(this, 
				"Message type is malformed, must have newline after message type.");
		return;
	}
	size -= type.size(); // 1 for newline
	type.chop(1); // remove newline

	if (type == "create-source") {
		handleCreateSourceMessage(size);
	} else if (type == "delete-source") {
		emit deleteSourceMessage(this);
	} else if (type == "set") {
		handleServerSetMessage(size);
	} else if (type == "get") {
		handleServerGetMessage(size);
	} else if (type == "set-source") {
		handleSourceSetMessage(size);
	} else if (type == "get-source") {
		handleSourceGetMessage(size);
	} else if (type == "start-recording") {
		emit startRecordingMessage(this);
	} else if (type == "stop-recording") {
		emit stopRecordingMessage(this);
	} else if (type == "get-data") {
		handleDataRequestMessage(size);
	} else if (type == "get-all-data") {
		handleAllDataRequestMessage(size);
	} else {
		emit messageError(this, "Unknown message type from client: " + type);
	}
}

void Client::handleCreateSourceMessage(quint32 size)
{
	auto type = m_socket->readLine();
	size -= type.size();
	type.chop(1);

	auto location = m_socket->read(size); // remainder of message
	emit createSourceMessage(this, type, location);
}

void Client::handleServerSetMessage(quint32 size)
{
	auto param = m_socket->readLine();
	size -= param.size();
	param.chop(1);

	QVariant value;
	if ( (param == "save-file") || (param == "save-directory") ) {
		value = m_socket->read(size); // filename or directory, should be a string
	} else if ( (param == "recording-length") || (param == "read-interval") ) {
		quint32 val = 0;
		m_socket->read(reinterpret_cast<char*>(&val), sizeof(val));
		value = val;
	} else {
		emit messageError(this, "Unknown server parameter: " + param);
		return;
	}
	emit setServerParamMessage(this, param, value);
}

void Client::handleServerGetMessage(quint32 /* size */)
{
	auto param = m_socket->readLine();
	param.chop(1);
	emit getServerParamMessage(this, param);
}

void Client::handleSourceSetMessage(quint32 size)
{
	auto param = m_socket->readLine();
	size -= param.size();
	param.chop(1);

	QByteArray buffer = m_socket->read(size);
	QVariant data = datasource::deserialize(param, buffer);
	emit setSourceParamMessage(this, param, data);
}

void Client::handleSourceGetMessage(quint32 size)
{
	auto param = m_socket->readLine();
	size -= param.size();
	param.chop(1);
	emit getSourceParamMessage(this, param);
}

void Client::handleDataRequestMessage(quint32 /* size */)
{
	float start, stop;
	m_stream >> start >> stop;
	emit dataRequest(this, start, stop);
}

void Client::handleAllDataRequestMessage(quint32 /* size */)
{
	m_stream >> m_requestedAllData;
	emit allDataRequest(this, m_requestedAllData);
}

void Client::sendSourceCreateResponse(bool success, const QByteArray& msg)
{
	QByteArray buffer { "source-created\n" };
	quint32 size = buffer.size() + sizeof(success) + msg.size();
	m_stream << size;
	m_stream.writeRawData(buffer.data(), buffer.size());
	m_stream << success;
	m_stream.writeRawData(msg.data(), msg.size());
}

void Client::sendSourceDeleteResponse(bool success, const QByteArray& msg)
{
	QByteArray buffer { "source-deleted\n" };
	quint32 size = buffer.size() + sizeof(success) + msg.size();
	m_stream << size;
	m_stream.writeRawData(buffer.data(), buffer.size());
	m_stream << success;
	m_stream.writeRawData(msg.data(), msg.size());
}

void Client::sendServerSetResponse(const QByteArray& param, bool success, 
		const QByteArray& msg)
{
	QByteArray buffer { "set\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(param);
	buffer.append("\n");
	buffer.append(msg);
	m_stream << buffer;
}

void Client::sendServerGetResponse(const QByteArray& param, bool success, 
		const QVariant& data)
{
	QByteArray buffer { "get\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(param);
	buffer.append("\n");
	buffer.append(encodeServerGetResponseData(param, data));
	m_stream << buffer;
}

void Client::sendSourceSetResponse(const QByteArray& param, bool success,
		const QByteArray& msg)
{
	QByteArray buffer { "set-source\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(param);
	buffer.append("\n");
	buffer.append(msg);
	m_stream << buffer;
}

void Client::sendSourceGetResponse(const QByteArray& param, bool success,
		const QVariant& data)
{
	QByteArray buffer { "get-source\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(param);
	buffer.append("\n");
	if (success) {
		buffer.append(datasource::serialize(param, data));
	} else {
		buffer.append(data.toByteArray()); // error message
	}
	m_stream << buffer;
}

void Client::sendStartRecordingResponse(bool success, const QByteArray& msg)
{
	QByteArray buffer { "recording-started\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(msg);
	m_stream << buffer;
}

void Client::sendStopRecordingResponse(bool success, const QByteArray& msg)
{
	QByteArray buffer { "recording-stopped\n" };
	buffer.append(reinterpret_cast<const char*>(&success), sizeof(success));
	buffer.append(msg);
	m_stream << buffer;
}

void Client::sendAllDataResponse(bool success, const QByteArray& msg)
{
	QByteArray buffer { "get-all-data\n" };
	quint32 size = buffer.size() + sizeof(success) + msg.size();
	m_stream << size;
	m_stream.writeRawData(buffer.data(), buffer.size());
	m_stream << success;
	m_stream.writeRawData(msg.data(), msg.size());
}

QByteArray Client::encodeServerGetResponseData(const QByteArray& param, 
		const QVariant& data)
{
	QByteArray buffer;
	if ( (param == "save-file") || 
			(param == "save-directory") ) {
		buffer = data.toByteArray(); // filename, should be a string
	} else if ( (param == "recording-length") ||
			(param == "read-interval") ) {
		buffer.resize(sizeof(quint32));
		quint32 length = static_cast<quint32>(data.toUInt());
		std::memcpy(buffer.data(), &length, sizeof(length));
	} else {
		/* Error message */
		buffer = data.toByteArray();
	}
	return buffer;
}

void Client::sendDataFrame(const DataFrame& frame)
{
	QByteArray msg { "data\n" };
	auto msgSize = msg.size();
	quint32 totalSize = msgSize + frame.bytesize();
	msg.resize(totalSize);
	frame.serializeInto(msg.data() + msgSize);
	m_stream << totalSize;
	m_socket->write(msg);
}

void Client::sendErrorMessage(const QByteArray& msg)
{
	QByteArray err { "error\n" };
	m_stream << (err + msg);
}

void Client::addPendingDataRequest(float start, float stop)
{
	m_pendingRequests.append({ start, stop });
}

void Client::setRequestedAllData(bool requested)
{
	m_requestedAllData = requested;
}

bool Client::requestedAllData() const
{
	return m_requestedAllData;
}

Client::DataRequest Client::nextPendingRequest()
{
	return m_pendingRequests.takeFirst();
}

int Client::countPendingRequests() const
{
	return m_pendingRequests.size();
}

int Client::numServicableRequests(float time) const
{
	return std::count_if(m_pendingRequests.begin(), m_pendingRequests.end(),
			[time](const Client::DataRequest& req) -> bool { 
				return req.stop <= time;
			});
}

