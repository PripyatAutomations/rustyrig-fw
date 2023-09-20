Sequence of Operation
---------------------
* First 10 seconds after reset:
	Once per second, try to initiate a receive file (send 'rz\r' string)
* After that, if file received and saved, continue. Else reboot into
	continuous file RX mode, awaiting a correct firmware...
