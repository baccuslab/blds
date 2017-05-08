/*! \file server.cc
 *
 * Implementation of main Server class for BLDS application.
 *
 * C)
 */

#include "server.h"

#include "libdatafile/include/hidensfile.h"

Server::Server(QObject* parent) :
	QObject(parent),
	source(nullptr),
	nclients(0),
	startTime(QDateTime::currentDateTime())
{
	readConfigFile();
	initServer();
	initStatusServer();
	QObject::connect(this, &Server::recordingFinished,
			this, &Server::handleRecordingFinished);
}

Server::~Server()
{
	/* Delete file, flushing any remaining data. */
	file.reset(nullptr);

	/* Close HTTP server */
	statusServer.close();
	for (auto& client : clients) {
		client->deleteLater();
	}

	/* Close main server */
	server->close();

	/* Delete source object and stop background thread */
	if (source)
		source->deleteLater();
	if (sourceThread)
		sourceThread->quit();
}

/*
 * Read blds.conf configuration file for some runtime settings, 
 * using defaults when those are not available.
 */
void Server::readConfigFile()
{
	/* Check if the blds.conf file exists */
	auto file = QFileInfo{QCoreApplication::applicationDirPath(), "blds.conf"};
	if (!file.exists()) {
		/* Try the above directory. */
		auto dir = file.dir();
		dir.cdUp();
		file = QFileInfo{dir.path(), "blds.conf"};
		if (!file.exists()) {
			qWarning("No configuration file found! Using defaults for all values.");
			httpPort = DefaultHttpPort;
			port = DefaultClientPort;
			maxConnections = DefaultMaxConnections;
			return;
		}
	}

	/* Read settings from the file */
	auto configFile = file.filePath();
	QSettings settings(configFile, QSettings::IniFormat);
	bool ok = true;

	/* Read HTTP port. */
	httpPort = settings.value("http-port", DefaultHttpPort).toUInt(&ok);
	if (!ok) {
		qWarning("Inavlid HTTP port in blds.conf, using default of %d", DefaultHttpPort);
		httpPort = DefaultHttpPort;
	}

	/* Read main server port */
	port = settings.value("port", DefaultClientPort).toUInt(&ok);
	if (!ok) {
		qWarning("Inavlid server port in blds.conf, using default of %d", DefaultClientPort);
		port = DefaultClientPort;
	}

	/* Read number of connections allowed */
	maxConnections = settings.value("max-connections", DefaultMaxConnections).toUInt(&ok);
	if (!ok) {
		qWarning("Invalid maximum number of connections in blds.conf, using default of %d",
				DefaultMaxConnections);
		maxConnections = DefaultMaxConnections;
	}
	
	/* Read length of a recording */
	recordingLength = settings.value("recording-length", DefaultRecordingLength).toUInt(&ok);
	if (!ok) {
		qWarning("Invalid recording length in blds.conf, using default of %d",
				DefaultRecordingLength);
		recordingLength = DefaultRecordingLength;
	}

	/* Read the interval between reads from the data source */
	readInterval = settings.value("read-interval", DefaultReadInterval).toUInt(&ok);
	if (!ok) {
		qWarning("Invalid source read interval in blds.conf, using default of %d",
				DefaultReadInterval);
		readInterval = DefaultReadInterval;
	}
	
	/* Maximum chunk size for data requests. */
	maxRequestChunkSize = settings.value("max-chunk-size", 
			MaximumDataRequestChunkSize).toDouble(&ok);
	if (!ok) {
		qWarning("Invalid maximum data chunk size in blds.conf, using default of %0.2f",
				MaximumDataRequestChunkSize);
	}

	/* Use default save directory to start */
	saveDirectory = DefaultSaveDirectory;
}

/*
 * Setup the main TCP server object used for communication
 * with remote clients.
 */
void Server::initServer()
{
	server = new QTcpServer(this);
	if (!server->listen(QHostAddress::Any, port)) {
		qFatal("Could not initialize main BLDS server.");
	}
	qInfo().nospace() << "Data sever listening on port " << server->serverPort() <<
		". Allowing up to " << maxConnections << " clients.";
	QObject::connect(server, &QTcpServer::newConnection,
			this, &Server::handleNewClient);
}


/*
 * Setup the HTTP server used to serve status requests.
 */
void Server::initStatusServer()
{
	if (!statusServer.listen(QHostAddress::Any, httpPort)) {
		qWarning("Could not initialize HTTP status server.");
	}
	QObject::connect(&statusServer, &Tufao::HttpServer::requestReady,
			this, &Server::handleHttpRequest);
	qInfo().nospace() << "HTTP status server listening at " <<
		statusServer.serverPort() << ".";
}

/*
 * Base handler for HTTP requests. Just delegates to appropriate sub-handler.
 */
void Server::handleHttpRequest(Tufao::HttpServerRequest& request,
		Tufao::HttpServerResponse& response)
{
	if (request.url().toString() == "/source") {
		serveSourceStatus(request, response);
	} else if (request.url().toString() == "/status") {
		serveStatus(request, response);
	} else {
		response.writeHead(404, "Not Found");
		response.end();
	}
}

/*
 * Handle HTTP requests for the status of the managed data source.
 */
void Server::serveSourceStatus(Tufao::HttpServerRequest& request,
		Tufao::HttpServerResponse& response)
{
	if ( (request.method() != "GET") && (request.method() != "HEAD") ) {
		response.writeHead(405, "Method Not Allowed");
		response.end();
		return;
	}
	if (!source) {
		response.writeHead(404, "Not Found");
		response.end();
		return;
	}
	response.writeHead(200, "OK");
	if (request.method() == "GET") {
		response.write(QJsonDocument(
					QJsonObject::fromVariantMap(sourceStatus)).toJson());
	}
	response.end();
}

/*
 * Handle HTTP requests for the status of the Server object itself.
 */
void Server::serveStatus(Tufao::HttpServerRequest& request,
		Tufao::HttpServerResponse& response)
{
	if ( (request.method() != "GET") && (request.method() != "HEAD") ) {
		response.writeHead(405, "Method Not Allowed");
		response.end();
		return;
	}
	response.writeHead(200, "OK");

	if (request.method() == "GET") {

		/* Insert address of all clients */
		QJsonArray dc;
		for (auto& c : clients) {
			dc.append(c->address());
		}

		/* Collect information about any current recording. */
		bool recordingExists = file != nullptr;
		double recordingPosition = 0.;
		if (recordingExists) {
			recordingPosition = file->length();
		}

		/* Collect information about any current data source. */
		bool sourceExists = source != nullptr;
		QString sourceType = (sourceExists ? 
				sourceStatus["source-type"].toString() : "none");

		/* Insert the server information */
		QJsonObject json {
				{ "start-time", startTime.toString() },
				{ "save-directory", saveDirectory },
				{ "save-file", saveFile }, 
				{ "recording-length", static_cast<qint64>(recordingLength) },
				{ "read-interval", static_cast<qint64>(readInterval) },
				{ "recording-exists", recordingExists },
				{ "recording-position", recordingPosition },
				{ "source-exists", sourceExists },
				{ "source-type", sourceType },
				{ "device-type", (sourceExists ? 
						sourceStatus["device-type"].toString() : "") },
				{ "source-location", (sourceExists ? 
						sourceStatus["location"].toString() : "") },
				{ "clients", dc }
		};

		/* Write response */
		response.write(QJsonDocument(json).toJson());
	}

	response.end();
}

/*
 * Handler for dealing with new remote client connections.
 */
void Server::handleNewClient()
{
	/* Get client socket, verify we have more available connections */
	auto socket = server->nextPendingConnection();
	if (nclients == maxConnections) {
		qWarning() << "Received connection attempt while already at maximum number" 
			<< " of connected clients. Ignoring the connection.";
		socket->deleteLater();
		return;
	}

	/* Create Client object to manage communication with this new client */
	auto *client = new Client(socket);
	connectClientSignals(client);
	nclients++;
	clients.append(client);
	qInfo().noquote() << "New client at" << client->address();
}

/*
 * Respond to a disconnection by a remote client.
 */
void Server::handleClientDisconnection(Client *client)
{
	qInfo().noquote() << "Client disconnected" << client->address();
	QObject::disconnect(client, 0, 0, 0);
	clients.removeOne(client);
	client->deleteLater();
	nclients--;
}

void Server::handleClientMessageError(Client *client, const QByteArray& msg)
{
	qWarning().noquote() << "Error communicating with client at" << client->address() 
		<< ":" << msg;
	client->sendErrorMessage(msg);
}

void Server::createFile()
{
	/* Create a save file name if not given by the clients. */
	if (saveFile.isNull() || (saveFile.size() == 0)) {
		saveFile = QDateTime::currentDateTime().toString(DefaultSaveFormat);
	}
	if (!saveFile.endsWith(".h5") && !saveFile.endsWith(".hdf5")) {
		saveFile.append(".h5");
	}

	/* Fail if the client requested an existing file.
	 * The server's default filenames are based on the current timestamp, and most
	 * likely won't already exist.
	 */
	auto fullpath = saveDirectory + "/" + saveFile;
	if (QFileInfo::exists(fullpath)) {
		throw std::invalid_argument("The requested file already exists, remove it first.");
	}

	/* Create the data file */
	auto path = fullpath.toStdString();
	auto type = sourceStatus["device-type"].toString();
	if (type.startsWith("hidens")) {
		auto nchannels = sourceStatus["nchannels"].toInt();
		auto f = new hidensfile::HidensFile(path, "hidens", nchannels);
		file.reset(f);
		f->setConfiguration(
				sourceStatus["configuration"].value<QConfiguration>().toStdVector());
	} else {
		file.reset(new datafile::DataFile(path));
		if (sourceStatus["has-analog-output"].toBool()) {
			int size = sourceStatus["analog-output"].value<QVector<double>>().size();
			file->setAnalogOutputSize(size);
		}
	}
	file->setGain(sourceStatus["gain"].toFloat());
	file->setOffset(sourceStatus["adc-range"].toFloat());
	file->setDate(QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
}

void Server::initSource()
{
	/* 
	 * Pass messages between server and source to communicate
	 * the source's state, served as JSON in response to HTTP
	 * requests.
	 */
	QObject::connect(this, &Server::requestSourceStatus,
			source, &datasource::BaseSource::requestStatus);
	QObject::connect(source, &datasource::BaseSource::status,
			this, [&](QVariantMap newStatus) {
				sourceStatus.swap(newStatus);
			});

	/*
	 * Pass messages indicating a client request for
	 * some state change, parameter change, etc from the 
	 * server to the source.
	 */
	QObject::connect(this, &Server::requestSourceInitialize,
			source, &datasource::BaseSource::initialize);
	QObject::connect(this, &Server::requestSourceGet,
			source, &datasource::BaseSource::get);
	QObject::connect(this, &Server::requestSourceSet,
			source, &datasource::BaseSource::set);
	QObject::connect(this, &Server::requestSourceStartStream,
			source, &datasource::BaseSource::startStream);
	QObject::connect(this, &Server::requestSourceStopStream,
			source, &datasource::BaseSource::stopStream);

	/*
	 * Pass messages indicating the response of the above
	 * client requests from the source to the server.
	 */
	QObject::connect(source, &datasource::BaseSource::error,
			this, &Server::handleSourceError);

	/* Request that the source initialize. */
	emit requestSourceInitialize();

}

void Server::deleteSource()
{
	QObject::disconnect(source, 0, 0, 0);
	source->deleteLater();
	source = nullptr;
}

void Server::handleSourceGetResponse(Client* client, const QString& param, 
		bool success, const QVariant& data)
{
	/* Disconnect this handler. */
	QObject::disconnect(source, &datasource::BaseSource::getResponse, 0, 0);

	if (success) {
		sourceStatus.insert(param, data);
	} else {
		qWarning().noquote() << "Error retrieving parameter from source:" 
			<< param;
	}
	client->sendSourceGetResponse(param.toUtf8(), success, data);
}

void Server::handleSourceSetResponse(Client *client,
		const QString& param, bool success, const QString& msg)
{
	/* Disconnect this handler */
	QObject::disconnect(source, &datasource::BaseSource::setResponse, 0, 0);

	if (success) {

		/* Request new status of the source. */
		emit requestSourceStatus();

		/* Notify */
		qInfo().noquote() << "Client at" << client->address() << 
			"successfully set parameter" << param;
	} else {

		/* Notify */
		qWarning().noquote().nospace() << "Parameter '" << param 
			<< "' not set: " << msg;

	}
	client->sendSourceSetResponse(param.toUtf8(), success, msg.toUtf8());
}

void Server::handleSourceInitialized(Client *client, bool success, const QString& msg)
{
	if (success) {

		/* Disconnect the initialized() handler */
		QObject::disconnect(source, &datasource::BaseSource::initialized, 0, 0);
		
		/* Request the status from the source */
		emit requestSourceStatus();

		/* Only move the source to a background thread if it is 
		 * NOT a FileSource.
		 *
		 * This is a work-around to deal with a fundamental lack of
		 * thread-safety in the HDF5 library. Very core components of
		 * the library (such as fundamental datatype objects) are not at 
		 * all thread safe, causing subtle race conditions. These occur
		 * even though the Server only reads from the source file,
		 * all reads/writes of the recording file occur in this main
		 * thread, and *these are completely different file objects*.
		 * It appears the HDF5 reference implementation maintains some
		 * global objects which are manipulated in a non-threadsafe 
		 * way, and these are shared between most or all library calls.
		 *
		 * Rather than build the library in threadsafe mode, which 
		 * precludes using the C++ interface (and thus the entire
		 * libdatafile shared library), just use a single thread 
		 * in that case. This may hurt performance a bit, especially
		 * when clients request large chunks of data, but in the
		 * case of replaying an old file, performance is not crucial.
		 */
		if (sourceStatus["source-type"] != "file") {
			sourceThread = new QThread(this);
			sourceThread->start();
			source->moveToThread(sourceThread);
		}

		qInfo().noquote() << "Data source successfully initialized by client"
			<< client->address();

	} else {

		qWarning().noquote() << "Could not initialize data source:" << msg;
		deleteSource(); // disconnects all signals
		
	}
	client->sendSourceCreateResponse(success, msg.toUtf8());
}

void Server::handleSourceStreamStarted(Client *client, bool success, const QString& msg)
{
	QObject::disconnect(source, &datasource::BaseSource::streamStarted, 0, 0);
	if (success) {
		qInfo().noquote() << "Recording started by client at" << client->address();
		qInfo().noquote() << "Recording data to" << saveDirectory + saveFile;
	} else {
		file.reset(nullptr);
		saveFile.clear();
		QObject::disconnect(source, &datasource::BaseSource::dataAvailable,
				this, &Server::handleNewDataAvailable);
		qWarning().noquote() << "Could not start recording:" << msg;
	}
	client->sendStartRecordingResponse(success, msg.toUtf8());
}

void Server::handleSourceStreamStopped(Client *client, bool success, const QString& msg)
{
	QObject::disconnect(source, &datasource::BaseSource::streamStopped, 0, 0);
	if (success) {
		qInfo().noquote() << "Recording stopped after" << file->length() 
			<< "seconds by client at" << client->address();
		file.reset(nullptr);
		saveFile.clear();
	} else {
		qWarning().noquote() << "Could not stop recording:" << msg;
	}
	client->sendStopRecordingResponse(success, msg.toUtf8());
}

void Server::handleSourceError(const QString& msg)
{
	qWarning().noquote() << "Error from data source:" << msg;
	while (clients.size()) {
		auto *client = clients.takeLast();
		client->sendErrorMessage(msg.toUtf8());
		client->deleteLater();
	}
	deleteSource();
}

void Server::handleNewDataAvailable(datasource::Samples samples)
{
	/* Append data to file */
	try {
		file->setData(file->nsamples(), file->nsamples() + samples.n_rows, samples);
	} catch (H5::Exception& e) {
		/* Error writing data to file. */
		for (auto client : clients) {
			client->sendErrorMessage(QString("An error occurred writing "
						"data to the recording file %1").arg(
						e.getDetailMsg().data()).toUtf8());
			client->deleteLater();
		}
		emit requestSourceStopStream();
		deleteSource();
		return;
	}

	if (nclients) {
		sendDataToClients(samples);
		servicePendingDataRequests();
	}

	/* Check if the recording is finished */
	checkRecordingFinished();
}

void Server::sendDataToClients(datasource::Samples& samples)
{
	/* Gather current timing information */
	auto sr = file->sampleRate();
	auto stopSample = file->nsamples();
	auto startSample = stopSample - samples.n_rows;
	auto start = static_cast<float>(startSample / sr);
	auto stop = static_cast<float>(stopSample / sr);

	/* Construct the current frame. This is not a target for optimization.
	 * This uses the move-constructor of the underlying data, so
	 * is very fast.
	 */
	DataFrame frame {start, stop, std::move(samples) };
	for (auto client : clients) {
		if (client->requestedAllData()) {
			client->sendDataFrame(frame);
		}
	}
}

void Server::servicePendingDataRequests()
{
	/* Service any outstanding request for data that can now be filled. */
	auto sr = file->sampleRate();
	auto currentTime = file->length();
	datasource::Samples samples;
	for (auto client : clients) {
		while (client->numServicableRequests(currentTime) > 0) {
			auto request = client->nextPendingRequest();
			auto begin = static_cast<int>(request.start * sr);
			auto end = static_cast<int>(request.stop * sr);
			try {
				file->data(begin, end, samples);
			} catch (std::logic_error& e) {
				client->sendErrorMessage(QString("Could not read requested "
							"data from file: %1").arg(
							e.what()).toUtf8());
				continue;
			}
			client->sendDataFrame({request.start, request.stop, 
					std::move(samples)});
		}
	}
}

void Server::handleClientCreateSourceMessage(Client *client,
		const QByteArray& type, const QByteArray& location)
{
	if (source) {
		/* There is an active data source, send error message. */
		QByteArray msg { "Cannot create data source while another exists." };
		qWarning().noquote() << msg;
		client->sendSourceCreateResponse(false, msg);

	} else {

		try {
			/* Create the source object. This throws a std::invalid_argument
			 * if the source couldn't be reached or created for some other 
			 * reason.
			 */
			source = datasource::create(QString::fromUtf8(type), 
					QString::fromUtf8(location), static_cast<int>(readInterval));

			/* Connect handler to the initialized() signal of the source. */
			QObject::connect(source, &datasource::BaseSource::initialized,
					this, [&, this, client](bool success, const QString& msg) -> void {
						handleSourceInitialized(client, success, msg);
					});

			/* Connect signals/slots for communication with the source,
			 * move it to the background thread, and send a request to
			 * it that it inititalize itself.
			 */
			initSource();
			return;

		} catch (std::invalid_argument& err) {
			QByteArray msg { "Could not create source! " };
			msg.append(err.what());
			qWarning().noquote() << msg;
			client->sendSourceCreateResponse(false, msg);
		}
	}
}

void Server::handleClientDeleteSourceMessage(Client *client)
{
	if (source) {
		if (file) {
			QByteArray msg { "Cannot delete source while recording is active, stop it first." };
			qWarning().noquote() << msg;
			client->sendSourceDeleteResponse(false, msg);
		} else {
			deleteSource();
			qInfo().noquote() << "Data source deleted by client at" << client->address();
			client->sendSourceDeleteResponse(true, "");
		}
	} else {
		QByteArray msg { "No source exists to be deleted." };
		qWarning().noquote() << msg;
		client->sendSourceDeleteResponse(false, msg);
	}
}

void Server::handleClientSetServerParamMessage(Client *client,
		const QByteArray& param, const QVariant& data)
{
	bool success = false;
	QByteArray msg;
	if (file) {
		/* Only allow setting server parameters when there is NOT an
		 * active recording.
		 */
		msg = "Cannot set server parameters while a recording is active. "
				"Stop it first.";
		qWarning().noquote() << msg;

	} else {

		if (param == "save-file") { 
			auto name = data.toString();
			auto path = saveDirectory + name;
			if (QFileInfo::exists(path)) {
				success = false;
				msg = QString("Save file at '%1' already exists, "
						"remove it first.").arg(path).toUtf8();
			} else {
				saveFile = name;
				qInfo().noquote() << "Client at" << client->address() 
					<< "set the save file to" << saveFile;
				success = true;
			}
		} else if (param == "save-directory") {
			auto dir = data.toString();
			if (QFileInfo::exists(dir)) {
				saveDirectory = dir;
				qInfo().noquote() << "Client at" << client->address() 
					<< "set the save directory to" << saveDirectory;
				success = true;
			} else {
				qWarning().noquote() << "Requested save directory" 
					<<  dir << "does not exist.";
				msg = QString("Requested save directory '%1' does not"
						" exist.").arg(dir).toUtf8();
				success = false;
			}
		} else if (param == "recording-length") {
			recordingLength = data.value<quint32>();
			qInfo().noquote() << "Client at" << client->address() 
				<< "set the recording length to" << recordingLength;
			success = true;
		} else if (param == "read-interval") {
			readInterval = data.value<quint32>();
			qInfo().noquote() << "Client at" << client->address() 
				<< "set the read interval to" << readInterval;
			success = true;
		}
	}
	client->sendServerSetResponse(param, success, msg);
}

void Server::handleClientGetServerParamMessage(Client *client, const QByteArray& param)
{
	bool valid;
	QVariant data;
	if (param == "save-file") {
		valid = true;
		data = saveFile.toUtf8();
	} else if (param == "recording-length") {
		valid = true;
		data = recordingLength;
	} else if (param == "save-directory") {
		valid = true;
		data = saveDirectory.toUtf8();
	} else if (param == "read-interval") {
		valid = true;
		data = readInterval;
	} else if (param == "recording-exists") {
		valid = true;
		data = file != nullptr;
	} else if (param == "recording-position") {
		valid = true;
		data = (file) ? static_cast<float>(file->length()) : 0.0f;
	} else if (param == "source-exists") {
		valid = true;
		data = source != nullptr;
	} else if (param == "source-type") {
		valid = true;
		data = sourceStatus["source-type"].toString().toUtf8();
	} else if (param == "start-time") {
		valid = true;
		data = startTime.toString().toUtf8();
	} else if (param == "source-location") {
		valid = true;
		data = sourceStatus["location"].toString().toUtf8();
	} else {
		valid = false;
		data = ("Unknown parameter type: " + param);
	}
	client->sendServerGetResponse(param, valid, data);
}

void Server::handleClientSetSourceParamMessage(Client *client, 
		const QByteArray& param, const QVariant& data)
{
	if (!source) {
		client->sendSourceSetResponse(param, false,
				"There is no data source to set parameters for.");
		return;
	}
	/* Connect handler for the response to notify the correct client. */
	QObject::connect(source, &datasource::BaseSource::setResponse,
			this, [this, client](const QString& param, bool success, 
					const QString& msg) -> void {
				handleSourceSetResponse(client, param, success, msg);
			});

	/* Request the source set the parameter */
	emit requestSourceSet(param, data);
}

void Server::handleClientGetSourceParamMessage(Client *client, const QByteArray& param)
{
	if (!source) {
		client->sendSourceGetResponse(param, false,
				"There is no active data source.");
		return;
	}

	/* Connect handler for the response to notify the correct client. */
	QObject::connect(source, &datasource::BaseSource::getResponse,
			this, [this, client](const QString& param, bool valid,
				const QVariant& data) -> void {
			handleSourceGetResponse(client, param, valid, data);
		});

	/* Request the parameter */
	emit requestSourceGet(param);
}

void Server::handleClientStartRecordingMessage(Client *client)
{
	QByteArray msg;
	if (source) {
		if (!file) {
			try {
				/* Create recording file. This throws a std::invalid_argument
				 * if the file already exists or couldn't be created for
				 * some other reason.
				 */
				createFile();

				/* Retrieve new data available from the source */
				QObject::connect(source, &datasource::BaseSource::dataAvailable,
						this, &Server::handleNewDataAvailable);

				/* Install handler for dealing with when the source stream starts. */
				QObject::connect(source, &datasource::BaseSource::streamStarted,
						this, [this, client](bool success, const QString& msg) -> void {
							handleSourceStreamStarted(client, success, msg);
					});

				/* Request that the source start. */
				emit requestSourceStartStream();
				return;

			} catch (std::invalid_argument& err) {
				msg = err.what();
			}
		} else {
			msg = "Cannot create recording, one is already active.";
		}
	} else {
		msg = "Cannot start recording, there is no active data source.";
	}
	
	qWarning().noquote() << msg;
	client->sendStartRecordingResponse(false, msg);
}

void Server::handleClientStopRecordingMessage(Client *client)
{
	QByteArray msg;
	if (source) {
		if (file) {

			/* Remove handler for new data */
			QObject::disconnect(source, &datasource::BaseSource::dataAvailable,
					this, &Server::handleNewDataAvailable);

			/* Install handler for dealing with when the source stream stops. */
			QObject::connect(source, &datasource::BaseSource::streamStopped,
					this, [this, client](bool success, const QString& msg) -> void {
						handleSourceStreamStopped(client, success, msg);
				});

			/* Request that the source stop its stream. */
			emit requestSourceStopStream();
			return;

		} else {
			msg = "Cannot stop recording, there is no recording to stop.";
		}
	} else {
		msg = "Cannot stop recording, there is no active data source.";
	}
	
	qWarning().noquote() << msg;
	client->sendStopRecordingResponse(false, msg);
}

void Server::handleClientDataRequest(Client *client, float start, float stop)
{
	if (file) {
		if (stop > recordingLength) {
			client->sendErrorMessage(
					"Cannot request more data than will exist in the recording");
		} else {

			/* Basic verification of the request */
			if (!verifyChunkRequest(start, stop)) {
				client->sendErrorMessage(
						QString("The requested data chunk is invalid. Both values must "
						"be positive, the second less than the first, and the resulting "
						" chunk size must be less than %1. The request was for [%2, %3)"
						).arg(maxRequestChunkSize).arg(start, 0, 'f', 1).arg(
						stop, 0, 'f', 1).toUtf8());
				return;
			}

			auto sr = file->sampleRate();
			auto endSample = static_cast<int>(stop * sr);
			if (file->nsamples() >= endSample) {

				/* If data is currently available, send it immediately */
				auto startSample = static_cast<int>(start * sr);
				DataFrame::Samples data;

				try {
					file->data(startSample, endSample, data);
				} catch (std::logic_error& e) {
					client->sendErrorMessage(QString("Could not read data "
								"from recording file: %1").arg(e.what()).toUtf8());
					return;
				}
				client->sendDataFrame(DataFrame{start, stop, std::move(data)});

			} else {
				/* Data is not yet available, add this to the list of pending
				 * data requests.
				 */
				client->addPendingDataRequest(start, stop);
			}
		}
	} else {
		client->sendErrorMessage("There is no active recording, data cannot be requested.");
	}
}

void Server::handleClientAllDataRequest(Client *client, bool requested)
{
	bool success = false;
	QByteArray msg;
	if ( !file || !requested) {
		/* Can only request all data if there is NOT a file. Can always
		 * cancel your request for all data.
		 */
		client->setRequestedAllData(requested);
		success = true;
	} else {
		msg = "Can only request all data before a recording starts. "
			"Data must now be requested in individual chunks.";
	}
	client->sendAllDataResponse(success, msg);
}

void Server::connectClientSignals(Client *client)
{
	QObject::connect(client, &Client::disconnected,
			this, &Server::handleClientDisconnection);
	QObject::connect(client, &Client::messageError,
			this, &Server::handleClientMessageError);
	QObject::connect(client, &Client::createSourceMessage,
			this, &Server::handleClientCreateSourceMessage);
	QObject::connect(client, &Client::deleteSourceMessage,
			this, &Server::handleClientDeleteSourceMessage);
	QObject::connect(client, &Client::setServerParamMessage,
			this, &Server::handleClientSetServerParamMessage);
	QObject::connect(client, &Client::getServerParamMessage,
			this, &Server::handleClientGetServerParamMessage);
	QObject::connect(client, &Client::setSourceParamMessage,
			this, &Server::handleClientSetSourceParamMessage);
	QObject::connect(client, &Client::getSourceParamMessage,
			this, &Server::handleClientGetSourceParamMessage);
	QObject::connect(client, &Client::startRecordingMessage,
			this, &Server::handleClientStartRecordingMessage);
	QObject::connect(client, &Client::stopRecordingMessage,
			this, &Server::handleClientStopRecordingMessage);
	QObject::connect(client, &Client::dataRequest,
			this, &Server::handleClientDataRequest);
	QObject::connect(client, &Client::allDataRequest,
			this, &Server::handleClientAllDataRequest);
}

void Server::checkRecordingFinished()
{
	if (file && file->length() >= static_cast<decltype(file->length())>(recordingLength)) {
		emit recordingFinished(file->length());
	}
}

void Server::handleRecordingFinished(double length)
{
	QObject::disconnect(source, &datasource::BaseSource::streamStopped, 0, 0);
	QObject::disconnect(source, &datasource::BaseSource::dataAvailable,
			this, &Server::handleNewDataAvailable);
	emit requestSourceStopStream();
	qInfo().noquote() << length << "seconds of data finished streaming to data file.";
	file.reset(nullptr);
	saveFile.clear();
}

bool Server::verifyChunkRequest(double start, double stop)
{
	return ( (start >= 0) && 
			(stop > (start + (1 / sourceStatus["sample-rate"].toDouble()))) && 
			((stop - start) <= maxRequestChunkSize));
}

