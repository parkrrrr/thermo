# thermo
Raspberry Pi web-enabled kiln management for Micromega CN77000-series controllers

This is a work-in-progress project that will eventually provide a web interface to program and monitor kilns equipped with an Omega Micromega CN77000-series RS-232-enabled setpoint controller. 

It is being developed for and tested with a model CN77554-C2 controller with output redirection enabled so that I can use setpoint 1 with my SSR. Whatever model of controller you use, you 

Your controller MUST be configured to echo commands, and it MUST NOT be configured for continuous output. The output format MUST be set to include only the PV.

At this point, it is possible to run and monitor existing programs, but the edit functionality isn't there yet. You can use an SQLite editor to create programs, though. (I just use the interactive sqlite3 shell, but that might not be sufficiently user-friendly.)

By default, the serial port is set up to use /dev/ttyUSB0 at 2400 8N1. The device and baud rate can be changed in the Settings table in thermo.db using your favorite SQLite editor. Logging is at 5-second intervals while firing and 60-second intervals while not firing, temperature tolerance is 5 degrees. Those may also be changed in the Settings table.

Installation is not yet implemented. CGI scripts will need to be copied to your CGI directory, which will also need to contain a symlink to thermo.db. the html directory should be copied to your web server. You'll have to arrange for thermo_server to be run somehow; I currently just run it from a shell logged in from another machine on the network, but that's not a good long-term solution.

[Issue tracking at overv.io](https://overv.io/parkrrrr/thermo/)
