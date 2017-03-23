/*! \file server.h
 *
 * Main class of the BLDS application, defining the server to which
 * clients connect and with which they communicate.
 *
 * (C) 2017 Benjamin Naecker bnaecker@stanford.edu
 */

#ifndef BLDS_SERVER_H
#define BLDS_SERVER_H

#include "client.h"
#include "data-frame.h"

#include "libdata-source/include/data-source.h"
#include "libdatafile/include/datafile.h"

#include <Tufao/HttpServer>
#include <Tufao/HttpServerRequest>
#include <Tufao/HttpServerResponse>

#include <QtCore>
#include <QtNetwork>

#include <memory>	// std::unique_ptr

/*! \class Server
 *
 * The Server class is the main object in the BLDS application. It manages
 * a data source on behalf of remote clients, and exposes a simple line-based
 * binary messaging protocol for interacting with the source. It also records
 * data from the source to disk and can send arbitrary chunks of data to clients
 * that request it.
 *
 * The Server treats all clients equally, meaning they are all given full
 * access to the source, including creation and deletion, and to the running
 * recording if it exists. This means that clients may not be fully aware of 
 * the state of the source or recording, and should always check the replies
 * from the Server for their requests.
 *
 * The Server manages a single data source and a single recording at a time.
 * Clients may also manipulate "parameters" of the Server itself, such as 
 * the save location of a recording, its duration, or how often the Server
 * reads data from the source (and thus sends it to the clients).
 *
 * Refer to the documentation for the Client class for details of the 
 * communication protocol.
 */
class Server : public QObject {
	Q_OBJECT

	/*! Port at which HTTP status server listens. */
	const quint16 DefaultHttpPort = 8000;

	/*! Port at which remote clients connect. */
	const quint16 DefaultClientPort = 12345;

	/*! Max simultaneous clients. */
	const int DefaultMaxConnections = 32;

	/*! Default length of a recording */
	const quint32 DefaultRecordingLength = 1000;

	/*! Default save directory */
	const QString DefaultSaveDirectory = { QDir::homePath() + "/Desktop/" };

	/* Default interval between reads from the data source, in milliseconds. */
	const quint32 DefaultReadInterval = 10;

	/*! Timestamp format for creating default filenames. */
	const QString DefaultSaveFormat = "yyyy-MM-ddTHH-mm-ss";

	/*! Maximum sized chunks to accept requests, in seconds. */
	const double MaximumDataRequestChunkSize = 10.0;
	
	public:

		/*! Construct a Server.
		 * \param parent The parent QObject.
		 *
		 * The Server is the main class of the BLDS application, and provides
		 * a remote interface to a managed data source on behalf of clients.
		 */
		Server(QObject *parent = nullptr);

		/*! Destroy the Server.
		 * 
		 * Any running recordings will be stopped immediately and no further
		 * data requests (pending or otherwise) will be serviced.
		 */
		~Server();

		/* Copying is explicitly disallowed */
		Server(const Server&) = delete;
		Server(Server&&) = delete;
		Server& operator=(const Server&) = delete;

	signals:
		/*! Request the source's full status.
		 *
		 * This sends a request to the managed source object that it
		 * reply wih the full current status, encoded as a QVariantMap
		 * of the relevant values.
		 */
		void requestSourceStatus();

		/*! Request that the source initialize itself.
		 *
		 * This requests that the source performs any necessary initialization
		 * before it may be fully interacted with. This includes making any
		 * remote connections, opening files, setting up default values, etc.
		 */
		void requestSourceInitialize();

		/*! Request to start the source's stream of data.
		 *
		 * This requests that the data source perform any final setup before
		 * data may be streamed, and start sending data as soon as it becomes
		 * available. If the request succeeds, no further parameters of the 
		 * source may be set, though they may still be queried.
		 */
		void requestSourceStartStream();

		/*! Request to stop the source's stream of data.
		 *
		 * This stops the stream of data from the source, and brings it
		 * to a state in which clients may manipulate its parameters. No
		 * further data requests will be allowed. However, if a pending
		 * request becomes servicable, it will be.
		 */
		void requestSourceStopStream();

		/*! Request the value of a named parameter from the source. */
		void requestSourceGet(const QString& param);

		/*! Request to set the named parameter of the data source to the given value.
		 *
		 * This is used to set parameters of the data source, before the stream
		 * of data is started.
		 */
		void requestSourceSet(const QString& param, const QVariant& value);

		/*! Emitted when the recording stops normally. */
		void recordingFinished(double length);

	private slots:
		/*! Handle an HTTP request for the source's or server's status.
		 *
		 * The Server exposes a simple HTTP interface, mostly intended for
		 * debugging or small queries. Two paths are supported:
		 * 	- /status - Returns the status of the Server itself
		 * 	- /source - Returns the status of the managed data source
		 */
		void handleHttpRequest(Tufao::HttpServerRequest& request,
				Tufao::HttpServerResponse& response);

		/*! Handler called when new clients connect. */
		void handleNewClient();

		/*! Handle the disconnection of a remote client. */
		void handleClientDisconnection(Client *client);

		/*! Handle a message from the client requesting the creation of a data source.
		 *
		 * \param type The type of source to create.
		 * \param location A location identifier for the source. For "file" sources,
		 * 	this is a local filename. For "hidens" sources, this is the IP address or
		 * 	hostname of the machine running the HiDens ThreadedServer program. For
		 * 	"mcs" sources, this is ignored, since the source is managed via a driver
		 * 	library that must be on the same machine.
		 */
		void handleClientCreateSourceMessage(Client *client, const QByteArray& type, 
				const QByteArray& location);

		/*! Handle a request from the client to delete a managed data source. */
		void handleClientDeleteSourceMessage(Client *client);

		/*! Handle a request from the client to set a parameter of the server.
		 * \param param The named parameter to be set.
		 * \param data The value to set the parameter to, encoded as a variant.
		 */
		void handleClientSetServerParamMessage(Client *client, 
				const QByteArray& param, const QVariant& data);

		/*! Handle a request from the client to get a named parameter of the server.
		 * \param param The name of the parameter to retrieve.
		 */
		void handleClientGetServerParamMessage(Client *client,
				const QByteArray& param);

		/*! Handle a request from the client to set a parameter of the data source.
		 * \param param The named parameter to be set.
		 * \param data The value to set the parameter to, encoded as a variant.
		 */
		void handleClientSetSourceParamMessage(Client *client, 
				const QByteArray& param, const QVariant& data);

		/*! Handle a request from the client to get a named parameter of the data source.
		 * \param param The name of the parameter to retrieve.
		 */
		void handleClientGetSourceParamMessage(Client *client,
				const QByteArray& param);

		/*! Handle a request from the client to start a recording.
		 *
		 * Clients should set the filename and length of the recording (and any
		 * other relevant parameters) before calling this function. Unless interrupted,
		 * the server will stream data to the file for the requested length of time.
		 */
		void handleClientStartRecordingMessage(Client *client);

		/*! Handle a request from the client to stop a recording. */
		void handleClientStopRecordingMessage(Client *client);

		/*! Handle a request for a chunk of data from the client.
		 * \param start The start time of the chunk to retrieve.
		 * \param stop The stop time of the chunk to retrieve.
		 *
		 * If the request cannot be serviced immediately, the server will 
		 * queue the request and send the relevant chunk of data to the client
		 * when it becomes available. 
		 *
		 * If the request will *never* be available, e.g. the client requests
		 * data past the end of the recording, the request will not be serviced
		 * and an error message will be returned.
		 */
		void handleClientDataRequest(Client *client, float start, float stop);

		/*! Handle a request from the client to get all available data.
		 *
		 * Clients may send this message to the Server in advance of starting
		 * a recording, to indicate that all data should be sent as soon as
		 * it is available. These will be sent as chunks of the same size and
		 * at the same rate as they are received from the data source. These
		 * messages are not valid if the recording has already started.
		 *
		 * \param client The client emitting the request.
		 * \param request True if the client indicates they want all data, false otherwise.
		 */
		void handleClientAllDataRequest(Client *client, bool request);

		/*! Handle a client messaging error.
		 * 
		 * \param client The client to which the error message should be sent.
		 * \param msg The error message to send.
		 */
		void handleClientMessageError(Client *client, const QByteArray& msg);

		/*! Handle a response to a request to get a named parameter of the source.
		 *
		 * \param client The client which sent the message. This is used to
		 * 	determine which client should be sent a reply.
		 * \param param The name of the parameter which the client requested be set.
		 * \param valid True if the request was for a valid parameter, else false.
		 * \param data If the request succeeded, this contains the parameter suitably
		 * 	encoded as a variant. If the request failed, this contains an error message,
		 * 	encoded as a QByteArray.
		 */
		void handleSourceGetResponse(Client* client, const QString& param, 
				bool valid, const QVariant& data);

		/*! Handle a response to a request to set a named parameter of the source.
		 *
		 * \param client The client which sent the message. This is used to
		 * 	determine which client should be sent a reply.
		 * \param param The name of the parameter which the client requested be set.
		 * \param success True if the request to set the parameter succeeded, else false.
		 * \param msg If the request to set the parameter failed, this contains an error.
		 */
		void handleSourceSetResponse(Client *client, 
				const QString& param, bool success, const QString& msg);

		/*! Handle the source initialization signal.
		 *
		 * \param client The client which sent the message. This is used to
		 * 	determine which client should be sent a reply.
		 * \param success True if the source was successfully initialized.
		 * \param msg If the request to initialize the source failed, this contains an error.
		 */
		void handleSourceInitialized(Client *client, bool success, const QString& msg);

		/*! Handle the source stream started signal.
		 *
		 * \param client The client which sent the message. This is used to
		 * 	determine which client should be sent a reply.
		 * \param success True if the stream was successfully started.
		 * \param msg If the request to start the stream failed, this contains an error.
		 */
		void handleSourceStreamStarted(Client *client, bool success, const QString& msg);

		/*! Handle the source stream stopped signal.
		 *
		 * \param client The client which sent the message. This is used to
		 * 	determine which client should be sent a reply.
		 * \param success True if the stream was successfully stopped.
		 * \param msg If the request to stop the stream failed, this contains an error.
		 */
		void handleSourceStreamStopped(Client *client, bool success, const QString& msg);

		/*! Handle an error from the source.
		 *
		 * \param msg The error message.
		 */
		void handleSourceError(const QString& msg);

		/*! Handle receipt of a new data chunk from the source.
		 *
		 * \param samples The data chunk received from the source.
		 */
		void handleNewDataAvailable(datasource::Samples samples);

		/*! Handle the end of a recording.
		 * 
		 * \param length The number of seconds of data recorded.
		 */
		void handleRecordingFinished(double length);

	private:

		/* Read the configuration file for runtime values of parameters. */
		void readConfigFile();

		/* Initialize the main server */
		void initServer();

		/* Initialize the HTTP status server. */
		void initStatusServer();

		/* Connect the signals and slots for communication with a new client. */
		void connectClientSignals(Client *client);

		/* Create a data file into which new data will be saved. */
		void createFile();

		/* Delete the currently-managed data source. */
		void deleteSource();

		/* Initialize the communcation between the data source and this object. */
		void initSource();

		/* Disconnect and delete a managed source. */
		void disconnectSource();

		/* Serve the status of the server itself to HTTP clients. */
		void serveStatus(Tufao::HttpServerRequest& request, 
				Tufao::HttpServerResponse& response);

		/* Serve the status of the data source to HTTP clients. */
		void serveSourceStatus(Tufao::HttpServerRequest& request, 
				Tufao::HttpServerResponse& response);

		/* Send data to any clients as it arrives. */
		void sendDataToClients(datasource::Samples& samples);

		/* Service any pending requests for data that have now
		 * become available.
		 */
		void servicePendingDataRequests();

		/* Check if the the server has collected enough data to 
		 * satisfy the requested length of the recording.
		 */
		void checkRecordingFinished();

		/* Return true if the given request is considered valid,
		 * and false otherwise.
		 */
		bool verifyChunkRequest(double start, double stop);

		/* Thread in which the source object lives. */
		QThread *sourceThread;

		/* The source object itself. */
		QPointer<datasource::BaseSource> source;

		/* Main server. */
		QTcpServer *server;

		/* Server port. */
		quint16 port;

		/* HTTP status server. */
		Tufao::HttpServer statusServer;

		/* HTTP status server port. */
		quint16 httpPort;

		/* Maximum number of connections. */
		int maxConnections;

		/* Maximum size of a data chunk to accept a request for, in seconds. */
		double maxRequestChunkSize;

		/* List of connected remote clients. */
		QList<Client*> clients;

		/* Number of clients. */
		int nclients;

		/* Map storing parameter names and values of the data source. */
		QVariantMap sourceStatus;

		/* Time at which the server started running. */
		QDateTime startTime;

		/* File to which data is to be saved. */
		std::unique_ptr<datafile::DataFile> file;

		/* Directory in which data will be saved. */
		QString saveDirectory;

		/* File to which data will be saved.
		 * If a client does not specifically set this, it will default to
		 * a value based on the timestamp when the client requests the 
		 * recording to start.
		 */
		QString saveFile;

		/* Client-defined amount of data to record. */
		quint32 recordingLength;

		/* Interval between reads from the data source. */
		quint32 readInterval;
};

#endif

