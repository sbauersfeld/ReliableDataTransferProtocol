# ReliableDataTransferProtocol
An FTP client and server that implements a reliable data transfer protocol similar to TCP. Additionally, serverCC.cpp, clientCC.cpp, and globalsCC.h implement a congestion control algorithm. Because these programs run over UDP, and UDP implements a checksum, the checksum portion of the reliable data transfer protocols has been commented out. In order to include checksums, simply uncomment those lines in globals.h and globalsCC.h.

More information can be found in report.pdf.
