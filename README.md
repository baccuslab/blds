# Baccus Lab Data Server

Server application which manages data sources on behalf of remote clients.

(C) 2016-2017 Benjamin Naecker bnaecker@stanford.edu

## Overview

The Baccus Lab Data Server (BLDS) is a server program which manages a data
source (such as an MEA or previously-recorded file) on behalf of client programs,
and which allows them to control the source, collect data from it, and request
that the server stream data to disk from the source.

Client programs connect using a simple, (mostly) text- and line-based messaging
protocol, which allows them to send messages to the BLDS and receive replies.
These messages allow clients to query, control, and collect data from the
data source managed by the BLDS, as well as request that the BLDS start and
stop recording data to disk on behalf of clients.
