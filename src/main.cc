/*! \file main.cc
 *
 * Main entry point for BLDS application.
 *
 * (C) 2017 Benjamin Naecker bnaecker@stanford.edu
 */

#include "server.h"

#include <QtCore>

#include <cstdio> 	// fopen, fclose, fprintf, fflush
#include <cstdlib> 	// abort, exit
#include <iostream>

/* File stream to which logging information is written. */
static FILE* logfile;

void logMessageHandler(QtMsgType type, const QMessageLogContext& context,
		const QString& message)
{
	auto msg = message.toLocal8Bit();
	auto date = QDateTime::currentDateTime().toString().toLocal8Bit();

	switch (type) {
		case QtDebugMsg:
			std::fprintf(logfile, "%s [debug]: %s (%s:%u %s)\n", date.constData(),
					msg.constData(), context.file, context.line, context.function);
			break;
		case QtInfoMsg:
			std::fprintf(logfile, "%s [info]: %s\n", date.constData(), msg.constData());
			break;
		case QtWarningMsg:
			std::fprintf(logfile, "%s [warning]: %s\n", date.constData(), msg.constData());
			break;
		case QtCriticalMsg:
			std::fprintf(logfile, "%s [critical]: %s (%s:%u %s)\n", date.constData(),
					msg.constData(), context.file, context.line, context.function);
			if (logfile != stdout) {
				std::fprintf(stdout, "%s [critical]: %s (%s:%u %s)\n", date.constData(),
						msg.constData(), context.file, context.line, context.function);
			}
			break;
		case QtFatalMsg:
			std::fprintf(logfile, "%s [fatal]: %s (%s:%u %s)\n", date.constData(),
					msg.constData(), context.file, context.line, context.function);
			if (logfile != stdout) {
				std::fprintf(stdout, "%s [fatal]: %s (%s:%u %s)\n", date.constData(),
						msg.constData(), context.file, context.line, context.function);
			}
			std::exit(EXIT_FAILURE);
	}
	std::fflush(logfile);
}

void setupLogging(bool quiet)
{
	if (quiet) {
		auto filename = QString(QDir::tempPath() + "/" + 
				QCoreApplication::applicationName() + "." +
				QString::number(QCoreApplication::applicationPid()) + ".log").toStdString();
		logfile = std::fopen(filename.c_str(), "w");
		if (!logfile) {
			std::cerr << "Could not open " << filename << " for logging. "
					<< "Falling back to standard output." << std::endl;
			logfile = stdout;
		}
	} else {
		logfile = stdout;
	}
	qInstallMessageHandler(logMessageHandler);
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	app.setApplicationVersion(BLDS_VERSION);
	app.setOrganizationName("baccus-lab");

	QCommandLineParser parser;
	parser.setApplicationDescription(
			"Serve data from arrays or files to remote clients\n"
			"(C) 2017 The Baccus Lab");
	parser.addVersionOption();
	parser.addHelpOption();
	parser.addOption({ "quiet",
			"Write logging information to a log file "
			"rather than the default standard output."});
	parser.process(app);
	setupLogging(parser.isSet("quiet"));

	Server server(&app);
	auto retval = app.exec();
	std::fclose(logfile);
	return retval;
}

