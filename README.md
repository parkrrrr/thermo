# thermo
Raspberry Pi web-enabled kiln management for Micromega CN77000-series controllers

This is a work-in-progress project that will eventually provide a web interface to program and monitor kilns equipped with an Omega Micromega CN77000-series RS-232-enabled setpoint controller. For now, there is no web functionality; only command-line.

At the moment, the communication protocol is fixed at 2400 8N1. To change it, you'll have to edit the source.

Requirements: 
* sqlite3
* libSQLiteCpp
* boost

To use it, run "server" and communicate with it through the other included programs.

[Issue tracking at overv.io](https://overv.io/parkrrrr/thermo/)
