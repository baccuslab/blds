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
	
	public:

		/*! Construct a Server.
		 * \param parent The parent QObject.
		 */
		Server(QObject *parent = nullptr);

		/*! Destroy the Server. */
		~Server();

		/* Copying is explicitly disallowed */
		Server(const Server&) = delete;
		Server(Server&&) = delete;
		Server& operator=(const Server&) = delete;

	signals:
		/*! Request the source's full status.
		 *
		 * This sends a request to the managed source object that it
		 * reply wih the full current status, encoded as a JSON array
		 * of the relevant values.
		 */
		void requestSourceStatus();

		/*! Request that the source initialize itself. */
		void requestSourceInitialize();

		/*! Request to start the source's stream of data. */
		void requestSourceStartStream();

		/*! Request to stop the source's stream of data. */
		void requestSourceStopStream();

		/*! Request the value of a named parameter from the source. */
		void requestSourceGet(const QString& param);

		/*! Request to set the named parameter of the data source to the given value. */
		void requestSourceSet(const QString& param, const QVariant& value);

		/*! Emitted when the recording stops normally. */
		void recordingFinished(double length);

	private slots:
		/*! Handle an HTTP request for the source's or server's status. */
		void handleHttpRequest(Tufao::HttpServerRequest& request,
				Tufao::HttpServerResponse& response);

		/*! Handler called when new clients connect. */
		void handleNewClient();

		/*! Handle the disconnection of a remote client. */
		void handleClientDisconnection(Client *client);

		/*! Handle a message from the client requesting the creation of a data source.
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
		 * If the request cannot be service immediately, the server will 
		 * queue the request and send the relevant chunk of data to the client
		 * when it becomes available. 
		 *
		 * If the request will *never* be available, e.g. the client requests
		 * data past the end of the recording, an error message will be returned.
		 */
		void handleClientDataRequest(Client *client, double start, double stop);

		/*! Handle a request from the client to get all available data.
		 * \param client The client emitting the request.
		 * \param request True if the client indicates they want all data, false otherwise.
		 */
		void handleClientAllDataRequest(Client *client, bool request);

		/*! Handle a client messaging error. */
		void handleClientMessageError(Client *client, const QByteArray& msg);

		/*! Handle a response to a get request from the source. */
		void handleSourceGetResponse(const QString& param, bool valid, const QVariant& data);

		/*! Handle a response to a set request from the source. */
		void handleSourceSetResponse(Client *client, 
				const QString& param, bool success, const QString& msg);

		/*! Handle the source initialization signal */
		void handleSourceInitialized(Client *client, bool success, const QString& msg);

		/*! Handle the source stream started signal. */
		void handleSourceStreamStarted(Client *client, bool success, const QString& msg);

		/*! Handle the source stream stopped signal. */
		void handleSourceStreamStopped(Client *client, bool success, const QString& msg);

		/*! Handle an error from the source */
		void handleSourceError(const QString& msg);

		/*! Handle receipt of a new data chunk from the source. */
		void handleNewDataAvailable(Samples samples);

		/*! Handle the end of a recording. */
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

		/* Create a source of the given type at the given location. */
		void createSource(const QByteArray& type, const QByteArray& location);

		void createFile();

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
		void sendDataToClients(const Samples& samples);

		void checkRecordingFinished();

		/* Thread in which the source object lives. */
		QThread *sourceThread;

		/* The source object itself. */
		QPointer<BaseSource> source;

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

