#ifndef NEXSD_PROTOCOL_H
#define NEXSD_PROTOCOL_H

#include <stddef.h>

/**
 * NeXs Daemon Protocol Constants
 * 
 * Communication happens over Unix Domain Sockets at /tmp/nexsd.sock
 * 
 * Request Format:
 * [256 bytes] Filename (null-terminated)
 * [Rest]      File Content
 * 
 * Response Format:
 * [String]    Compiler errors in 'file:line:col: severity: message' format
 *             OR 'OK\n' if no errors found.
 */

#define SOCKET_PATH "/tmp/nexsd.sock"
#define LOCK_FILE   "/tmp/nexsd.lock"
#define MAX_FILENAME 256
#define BUF_SIZE 65536

#define CMD_SHUTDOWN "SHUTDOWN"

#endif /* NEXSD_PROTOCOL_H */
