/**
 * Common Header File for P2P File Sharing System
 * Contains shared utilities, constants, and helper functions
 */

#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <cstring>
#include <thread>
#include <mutex>
#include <algorithm>

// Network headers
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>

// Crypto headers
#include <openssl/sha.h>

using namespace std;

// ==================== CONSTANTS ====================

const int CHUNK_SIZE = 524288;      // 512 KB per chunk
const int BUFFER_SIZE = 8192;       // 8 KB buffer for general use
const int MAX_BUFFER = 65536;       // 64 KB max message size
const string DELIMITER = "$$";      // Message delimiter

// ==================== SOCKET UTILITIES ====================

/**
 * Receive exact number of bytes — low-level helper used by recv_data.
 * Returns: received string, empty on connection close or error.
 */
inline string recv_exact(int socket, size_t exact_size) {
    string result;
    result.reserve(exact_size);

    while (result.size() < exact_size) {
        size_t remaining = exact_size - result.size();
        vector<char> buffer(min(remaining, (size_t)BUFFER_SIZE));

        ssize_t received = recv(socket, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            if (received < 0 && errno == EINTR) continue;
            return "";
        }
        result.append(buffer.data(), received);
    }
    return result;
}

/**
 * Send data with a 4-byte length-prefix frame.
 * Every message is preceded by its size in network byte order so the
 * receiver knows exactly how many bytes to read — fixes TCP stream
 * fragmentation and the 64 KB silent-truncation bug.
 * Returns: bytes of body sent on success, -1 on failure.
 */
inline ssize_t send_data(int socket, const string& data) {
    // --- header: 4-byte body length in network byte order ---
    uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));
    const char* hdr = reinterpret_cast<const char*>(&net_len);
    ssize_t hdr_sent = 0;
    while (hdr_sent < 4) {
        ssize_t n = send(socket, hdr + hdr_sent, 4 - hdr_sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            perror("send_data header failed");
            return -1;
        }
        hdr_sent += n;
    }

    // --- body ---
    ssize_t total_sent = 0;
    ssize_t data_len = static_cast<ssize_t>(data.size());
    const char* ptr = data.c_str();
    while (total_sent < data_len) {
        ssize_t n = send(socket, ptr + total_sent, data_len - total_sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            perror("send_data body failed");
            return -1;
        }
        total_sent += n;
    }
    return total_sent;
}

/**
 * Receive one framed message: read the 4-byte length header, then read
 * exactly that many bytes.  No size cap — the sender controls length.
 * The unused max_size parameter is kept so all existing call sites compile
 * without modification.
 * Returns: message body, empty on connection close or error.
 */
inline string recv_data(int socket, size_t /*max_size*/ = MAX_BUFFER) {
    // --- header: 4-byte body length in network byte order ---
    uint32_t net_len = 0;
    char* hdr = reinterpret_cast<char*>(&net_len);
    size_t hdr_received = 0;
    while (hdr_received < 4) {
        ssize_t n = recv(socket, hdr + hdr_received, 4 - hdr_received, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) perror("recv_data header failed");
            return "";
        }
        hdr_received += n;
    }

    uint32_t body_len = ntohl(net_len);
    if (body_len == 0) return "";

    // --- body: read exactly body_len bytes ---
    return recv_exact(socket, body_len);
}

/**
 * Create and configure a TCP socket
 * Returns: socket fd on success, -1 on failure
 */
inline int create_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Allow socket reuse
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(sock);
        return -1;
    }
    
    return sock;
}

/**
 * Connect to a server
 * Returns: 0 on success, -1 on failure
 */
inline int connect_to_server(int socket, const string& ip, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Invalid address: " << ip << endl;
        return -1;
    }
    
    if (connect(socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }
    
    return 0;
}

/**
 * Bind socket to address and start listening
 * Returns: 0 on success, -1 on failure
 */
inline int bind_and_listen(int socket, const string& ip, int port, int backlog = 10) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        cerr << "Invalid address: " << ip << endl;
        return -1;
    }
    
    if (bind(socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return -1;
    }
    
    if (listen(socket, backlog) < 0) {
        perror("Listen failed");
        return -1;
    }
    
    return 0;
}

// ==================== STRING UTILITIES ====================

/**
 * Split string by delimiter
 */
inline vector<string> split_string(const string& str, const string& delim = DELIMITER) {
    vector<string> tokens;
    size_t start = 0;
    size_t end = 0;
    
    while ((end = str.find(delim, start)) != string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delim.length();
    }
    tokens.push_back(str.substr(start));
    
    return tokens;
}

/**
 * Join strings with delimiter
 */
inline string join_string(const vector<string>& parts, const string& delim = DELIMITER) {
    string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += delim;
        result += parts[i];
    }
    return result;
}

/**
 * Trim whitespace from string
 */
inline string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

// ==================== FILE UTILITIES ====================

/**
 * Get file size
 * Returns: file size in bytes, -1 on failure
 */
inline long long get_file_size(const string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        return -1;
    }
    return st.st_size;
}

/**
 * Check if path exists
 */
inline bool path_exists(const string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

/**
 * Check if path is a directory
 */
inline bool is_directory(const string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

/**
 * Get filename from path
 */
inline string get_filename(const string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return path;
    return path.substr(pos + 1);
}

// ==================== HASH UTILITIES ====================

/**
 * Calculate SHA1 hash of data
 * Returns: hex string of hash
 */
inline string sha1_hash(const string& data) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    
    char hex[SHA_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return string(hex);
}

/**
 * Calculate SHA256 hash of data
 * Returns: hex string of hash
 */
inline string sha256_hash(const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return string(hex);
}

/**
 * Calculate piecewise SHA1 hashes for a file
 * Returns: vector of hash strings for each chunk
 */
inline vector<string> calculate_piece_hashes(const string& filepath) {
    vector<string> hashes;
    ifstream file(filepath, ios::binary);
    
    if (!file.is_open()) {
        cerr << "Cannot open file: " << filepath << endl;
        return hashes;
    }
    
    vector<char> buffer(CHUNK_SIZE);
    while (file.read(buffer.data(), CHUNK_SIZE) || file.gcount() > 0) {
        string chunk(buffer.data(), file.gcount());
        hashes.push_back(sha1_hash(chunk));
    }
    
    return hashes;
}

// ==================== LOGGING ====================

inline mutex log_mutex;

inline void log_message(const string& msg, const string& level = "INFO") {
    lock_guard<mutex> lock(log_mutex);
    cout << "[" << level << "] " << msg << endl;
}

inline void log_error(const string& msg) {
    log_message(msg, "ERROR");
}

inline void log_info(const string& msg) {
    log_message(msg, "INFO");
}

inline void log_debug(const string& msg) {
    #ifdef DEBUG
    log_message(msg, "DEBUG");
    #endif
}

#endif // COMMON_H
