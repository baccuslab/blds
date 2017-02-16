/*! client.h
 *
 * Class for managing remote clients to tbe BLDS server.
 *
 * (C) 2017 Benjamin Naecker bnaecker@stanford.edu
 */

#ifndef BLDS_CLIENT_H
#define BLDS_CLIENT_H

#include "data-frame.h"

#include <QtCore>
#include <QtNetwork>

/*! \class Client
 * The Client class represents a remote client of the BLDS.
 *
 * This class implements the communcation protocol, both receiving and sending,
 * between the server and client.
 */
class Client : public QObject {
	Q_OBJECT

	public:

		/*! Simple structure used internally to manage pending
		 * requests for data.
		 */
		struct DataRequest {
			float start;
			float stop;
		};

		/*! Construct a Client from a socket. */
		Client(QTcpSocket* socket, QObject* parent = nullptr);

		/*! Destroy a client. */
		~Client();

		/*! Copying is not allowed. */
		Client(const Client&) = delete;
		Client(Client&&) = delete;
		Client& operator=(const Client&) = delete;

		/*! Return the remote IP address and port number. */
		inline QString address() const
		{
			return (m_socket->peerAddress().toString() + ":" +
					QString::number(m_socket->peerPort()));
		}

		/*! Return whether the client has requested all data. */
		bool requestedAllData() const;

		/*! Set whether the client is expecting all data. */
		void setRequestedAllData(bool requested);

		/*! Add a pending request for data.
		 * \param start The start time of the chunk of data requested.
		 * \param stop The stop time of the chunk of data requested.
		 * No checks are performed that the data hasn't already been sent, 
		 * nor are attempts made to coalesce data into fewer chunks or 
		 * de-duplicate frames sent to the client.
		 */
		void addPendingDataRequest(float start, float stop);

		/*! Return the number of pending data requests. */
		int countPendingRequests() const;

		/*! Return the next pending request, if one exists.
		 * This method should NOT be called if there are no pending requests.
		 * Use countPendingRequests() to determine if any exist.
		 */
		DataRequest nextPendingRequest();

		/* Return the number of servicable requests, based on the time.
		 * \param time Requests that end before this time are considered servicable.
		 */
		int numServicableRequests(float time) const;

	public slots:

		/*! Send this Client a response to a request to create a data source.
		 * \param success True if the request succeeded, else false.
		 * \param msg If the request failed, this contains an error message.
		 */
		void sendSourceCreateResponse(bool success, const QByteArray& msg = "");

		/*! Send this Client a response to a request to delete the current data source.
		 * \param success True if the request succeeded, else false.
		 * \param msg If the request failed, this contains an error message.
		 */
		void sendSourceDeleteResponse(bool success, const QByteArray& msg = "");

		/*! Send this Client a response to a request to set a parameter of the BLDS.
		 * \param param The name of the parameter that was requested.
		 * \param success True if the request succeede, else false.
		 * \param msg If the request failed, this contains an error message.
		 */
		void sendServerSetResponse(const QByteArray& param, bool success, 
				const QByteArray& msg = "");

		/*! Send the Client the value of the named parameter of the BLDS.
		 * \param param The name of the parameter that was requested.
		 * \param success True if the request succeeded, else false.
		 * \param data If the request succeeded, this contains the value, suitably
		 * 	encoded. If the request failed, this contains an error message.
		 */
		void sendServerGetResponse(const QByteArray& param, bool success,
				const QVariant& data = QVariant());

		/*! Send the Client a response to a request to set a parameter of the data source.
		 * \param param The name of the parameter the client requested be set.
		 * \param success True if the request succeeded, false otherwise.
		 * \param msg If the request failed, this is an error message.
		 */
		void sendSourceSetResponse(const QByteArray& param, bool success,
				const QByteArray& msg = "");

		/*! Send the Client the value of the named parameter of the data source.
		 * \param param The name of the parameter that was requested.
		 * \param success True if the request succeeded, else false.
		 * \param data If the request succeeded, this contains the value, suitably
		 * 	encoded. If the request failed, this contains an error message.
		 */
		void sendSourceGetResponse(const QByteArray& param, bool success,
				const QVariant& data = QVariant());

		/*! Send a response to a request to start the recording.
		 * \param success True if the request succeeded, else false.
		 * \param msg If the request failed, this is an error message.
		 */
		void sendStartRecordingResponse(bool success, const QByteArray& msg = "");

		/*! Send a response to a request to start the recording.
		 * \param success True if the request succeeded, else false.
		 * \param msg If the request failed, this is an error message.
		 */
		void sendStopRecordingResponse(bool success, const QByteArray& msg = "");

		/*! Send a response to a request for all data.
		 * \param success True if the request succeeded, false otherwise.
		 * \param msg If the request failed, this is an error message.
		 */
		void sendAllDataResponse(bool success, const QByteArray& msg = "");

		/*! Send the client an error message. */
		void sendErrorMessage(const QByteArray& msg);

		/*! Send the client the given frame of data. */
		void sendDataFrame(const DataFrame& frame);

	signals:

		/*! Emitted when the client disconnects.
		 * \param client The client which received the message.
		 */
		void disconnected(Client *client);

		/*! Emitted when an error occurs communicating with the client, e.g.,
		 * if the client sends an unrecognized message type.
		 * \param client The client which received the message.
		 */
		void messageError(Client *client, const QByteArray& error);

		/*! Emitted when the client requests that the BLDS create a data source.
		 * \param client The client which received the message.
		 * \param type The type of data source to be created.
		 * \param location A location identifying the source.
		 */
		void createSourceMessage(Client *client, 
				const QByteArray& type, const QByteArray& location);

		/*! Emitted when the client requests the BLDS delete the current data source.
		 * \param client The client which received the message.
		 */
		void deleteSourceMessage(Client *client);

		/*! Emitted when the client requests to set a named parameter of the BLDS.
		 * \param client The client which received the message.
		 * \param param The name of the parameter to be set.
		 * \param data The value of the parameter, suitably encoded.
		 */
		void setServerParamMessage(Client *client, const QByteArray& param, const QVariant& data);

		/*! Emitted when the client requests to retrieve a named parameter of the BLDS.
		 * \param client The client which received the message.
		 */
		void getServerParamMessage(Client *client, const QByteArray& param);

		/*! Emitted when the client requests to set a named parameter of the data source.
		 * \param client The client which received the message.
		 * \param param The name of the parameter to be set.
		 * \param data The value of the parameter, suitably encoded.
		 */
		void setSourceParamMessage(Client *client, 
				const QByteArray& param, const QVariant& data);

		/*! Emitted when the client requests to retrieve a named parameter of the BLDS.
		 * \param client The client which received the message.
		 */
		void getSourceParamMessage(Client *client, const QByteArray& param);

		/*! Emitted when the client requests the BLDS start a recording.
		 * \param client The client which received the message.
		 */
		void startRecordingMessage(Client *client);

		/*! Emitted when the client requests that the BLDS stop a recording.
		 * \param client The client which received the message.
		 */
		void stopRecordingMessage(Client *client);

		/*! Emitted when the client requests a chunk of data from the managed source.
		 * \param client The client which received the message.
		 * \param start The start time of the data chunk to receive.
		 * \param stop The stop time of the data chunk to receive.
		 */
		void dataRequest(Client *client, float start, float stop);

		/*! Emitted when the client requests all available data from managed source.
		 * \param client The client which received the message.
		 * \param requested True if client would like all data, false if they want
		 * 	to cancel a previous request for all data.
		 */
		void allDataRequest(Client *client, bool requested);

	private:
		void handleReadyRead();
		void handleMessage(quint32 size);
		void handleCreateSourceMessage(quint32 size);
		void handleServerSetMessage(quint32 size);
		void handleSourceSetMessage(quint32 size);
		void handleServerGetMessage(quint32 size);
		void handleSourceGetMessage(quint32 size);
		void handleDataRequestMessage(quint32 size);
		void handleAllDataRequestMessage(quint32 size);
		QByteArray encodeServerGetResponseData(const QByteArray& param, 
				const QVariant& data);
		QByteArray encodeSourceGetResponseData(const QByteArray& param, 
				const QVariant& data);

		QTcpSocket* m_socket;
		QDataStream m_stream;
		QLinkedList<DataRequest> m_pendingRequests;
		bool m_requestedAllData;
};

#endif

