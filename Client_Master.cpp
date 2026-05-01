/**
 * Client/Peer for P2P File Sharing System
 * 
 * This client acts as both:
 * - Client: Connects to tracker and other peers to download files
 * - Server: Serves file chunks to other peers
 * 
 * Usage: ./client <client_ip> <client_port> <tracker_ip> <tracker_port>
 * Example: ./client 192.168.1.101 6000 192.168.1.100 5000
 */

#include "common.h"
#include <atomic>
#include <queue>
#include <future>
#include <random>

// ==================== GLOBAL STATE ====================

// Client network info
string my_ip;
int my_port;
string tracker_ip;
int tracker_port;

// Login state
atomic<bool> is_logged_in(false);
string current_user;

// File state
// filename -> filepath (for files we're sharing)
map<string, string> shared_files;

// filename -> bitvector (which chunks we have)
map<string, vector<bool>> file_chunks;

// filename -> piece hashes (for verification)
map<string, vector<string>> file_hashes;

// Downloaded files: filename -> group_id
map<string, string> downloaded_files;

// Mutex for thread-safe operations
mutex file_mutex;

// Server running flag
atomic<bool> server_running(true);

// ==================== PEER SERVER ====================

/**
 * Handle request from another peer
 */
void handle_peer_request(int peer_socket) {
    string request = recv_data(peer_socket);
    
    if (request.empty()) {
        close(peer_socket);
        return;
    }
    
    vector<string> parts = split_string(request);
    
    if (parts.empty()) {
        close(peer_socket);
        return;
    }
    
    string command = parts[0];
    
    if (command == "GET_BITVECTOR" && parts.size() >= 2) {
        // Return which chunks we have for a file
        string filename = parts[1];
        
        lock_guard<mutex> lock(file_mutex);
        
        if (file_chunks.find(filename) == file_chunks.end()) {
            send_data(peer_socket, "ERROR$$File not found");
        } else {
            string bitvector;
            for (bool has_chunk : file_chunks[filename]) {
                bitvector += (has_chunk ? "1" : "0");
            }
            send_data(peer_socket, "OK$$" + bitvector);
        }
    }
    else if (command == "GET_CHUNK" && parts.size() >= 3) {
        // Send a specific chunk
        string filename = parts[1];
        int chunk_num = stoi(parts[2]);
        
        string filepath;
        {
            lock_guard<mutex> lock(file_mutex);
            if (shared_files.find(filename) == shared_files.end()) {
                send_data(peer_socket, "ERROR$$File not found");
                close(peer_socket);
                return;
            }
            filepath = shared_files[filename];
        }
        
        // Read the chunk from file
        ifstream file(filepath, ios::binary);
        if (!file.is_open()) {
            send_data(peer_socket, "ERROR$$Cannot open file");
            close(peer_socket);
            return;
        }
        
        file.seekg((long long)chunk_num * CHUNK_SIZE);
        vector<char> buffer(CHUNK_SIZE);
        file.read(buffer.data(), CHUNK_SIZE);
        size_t bytes_read = file.gcount();
        file.close();
        
        // Send chunk data
        string chunk_data(buffer.data(), bytes_read);
        
        // First send size, then data
        send_data(peer_socket, "OK$$" + to_string(bytes_read));
        
        // Wait for acknowledgment
        string ack = recv_data(peer_socket);
        if (ack != "READY") {
            close(peer_socket);
            return;
        }
        
        // Send raw chunk data
        ssize_t sent = 0;
        while (sent < (ssize_t)bytes_read) {
            ssize_t n = send(peer_socket, buffer.data() + sent, bytes_read - sent, MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += n;
        }
        
        log_debug("Sent chunk " + to_string(chunk_num) + " of " + filename);
    }
    else {
        send_data(peer_socket, "ERROR$$Unknown command");
    }
    
    close(peer_socket);
}

/**
 * Run peer server in background thread
 */
void run_peer_server() {
    int server_socket = create_socket();
    if (server_socket < 0) {
        log_error("Failed to create peer server socket");
        return;
    }
    
    if (bind_and_listen(server_socket, my_ip, my_port) < 0) {
        close(server_socket);
        log_error("Failed to bind peer server");
        return;
    }
    
    log_info("Peer server running on " + my_ip + ":" + to_string(my_port));
    
    while (server_running) {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        
        int peer_socket = accept(server_socket, (struct sockaddr*)&peer_addr, &addr_len);
        if (peer_socket < 0) {
            if (server_running) perror("Accept failed");
            continue;
        }
        
        // Handle peer request in new thread
        thread peer_thread(handle_peer_request, peer_socket);
        peer_thread.detach();
    }
    
    close(server_socket);
}

// ==================== PEER-TO-PEER DOWNLOAD ====================

/**
 * Get bitvector from a peer
 * Returns: bitvector string or empty on failure
 */
string get_peer_bitvector(const string& peer_addr, const string& filename) {
    vector<string> addr_parts = split_string(peer_addr, ":");
    if (addr_parts.size() != 2) return "";
    
    int sock = create_socket();
    if (sock < 0) return "";
    
    if (connect_to_server(sock, addr_parts[0], stoi(addr_parts[1])) < 0) {
        close(sock);
        return "";
    }
    
    send_data(sock, "GET_BITVECTOR$$" + filename);
    string response = recv_data(sock);
    close(sock);
    
    vector<string> parts = split_string(response);
    if (parts.size() >= 2 && parts[0] == "OK") {
        return parts[1];
    }
    
    return "";
}

/**
 * Download a chunk from a peer
 * Returns: true on success
 */
bool download_chunk(const string& peer_addr, const string& filename, 
                    int chunk_num, const string& dest_path, const string& expected_hash) {
    vector<string> addr_parts = split_string(peer_addr, ":");
    if (addr_parts.size() != 2) return false;
    
    int sock = create_socket();
    if (sock < 0) return false;
    
    if (connect_to_server(sock, addr_parts[0], stoi(addr_parts[1])) < 0) {
        close(sock);
        return false;
    }
    
    // Request chunk
    send_data(sock, "GET_CHUNK$$" + filename + "$$" + to_string(chunk_num));
    
    // Get response with chunk size
    string response = recv_data(sock);
    vector<string> parts = split_string(response);
    
    if (parts.size() < 2 || parts[0] != "OK") {
        close(sock);
        return false;
    }
    
    size_t chunk_size = stoul(parts[1]);
    
    // Send ready acknowledgment
    send_data(sock, "READY");
    
    // Receive chunk data
    vector<char> buffer(chunk_size);
    size_t received = 0;
    
    while (received < chunk_size) {
        ssize_t n = recv(sock, buffer.data() + received, chunk_size - received, 0);
        if (n <= 0) {
            close(sock);
            return false;
        }
        received += n;
    }
    
    close(sock);
    
    // Verify hash
    string chunk_data(buffer.data(), chunk_size);
    string actual_hash = sha1_hash(chunk_data);
    
    if (actual_hash != expected_hash) {
        log_error("Hash mismatch for chunk " + to_string(chunk_num));
        return false;
    }
    
    // Write to destination file
    fstream file(dest_path, ios::in | ios::out | ios::binary);
    if (!file.is_open()) {
        log_error("Cannot open destination file");
        return false;
    }
    
    file.seekp((long long)chunk_num * CHUNK_SIZE);
    file.write(buffer.data(), chunk_size);
    file.close();
    
    return true;
}

// ==================== PARALLEL DOWNLOAD INFRASTRUCTURE ====================

const int MAX_PARALLEL_DOWNLOADS = 4;

// State shared across all download worker threads.
struct DownloadState {
    mutex                    mtx;
    queue<int>               pending;            // chunk indices yet to be downloaded
    map<int, vector<string>> chunk_availability; // mutable: failed peers are erased
    atomic<int>              chunks_done{0};
    atomic<bool>             fatal_error{false};
    int                      num_chunks = 0;
};

// Like download_chunk but writes with pwrite() instead of fstream::seekp.
// pwrite() is POSIX-guaranteed atomic for non-overlapping file regions, so
// multiple threads can write different chunks to the same fd concurrently
// without any mutex.
static bool download_chunk_to_fd(const string& peer_addr, const string& filename,
                                  int chunk_num, int fd, const string& expected_hash) {
    vector<string> addr_parts = split_string(peer_addr, ":");
    if (addr_parts.size() != 2) return false;

    int sock = create_socket();
    if (sock < 0) return false;

    if (connect_to_server(sock, addr_parts[0], stoi(addr_parts[1])) < 0) {
        close(sock);
        return false;
    }

    send_data(sock, "GET_CHUNK$$" + filename + "$$" + to_string(chunk_num));

    string response = recv_data(sock);
    vector<string> parts = split_string(response);
    if (parts.size() < 2 || parts[0] != "OK") {
        close(sock);
        return false;
    }

    size_t chunk_size = stoul(parts[1]);
    send_data(sock, "READY");

    vector<char> buffer(chunk_size);
    size_t received = 0;
    while (received < chunk_size) {
        ssize_t n = recv(sock, buffer.data() + received, chunk_size - received, 0);
        if (n <= 0) { close(sock); return false; }
        received += n;
    }
    close(sock);

    // Verify SHA1 of this chunk
    string actual_hash = sha1_hash(string(buffer.data(), chunk_size));
    if (actual_hash != expected_hash) {
        log_error("Hash mismatch for chunk " + to_string(chunk_num));
        return false;
    }

    // Write at the exact file offset — no seek, no lock needed
    ssize_t written = pwrite(fd, buffer.data(), chunk_size,
                             static_cast<off_t>(chunk_num) * CHUNK_SIZE);
    return written == static_cast<ssize_t>(chunk_size);
}

// Worker: drain the shared pending queue.  On failure, blacklist the peer
// for that chunk and re-queue it so another worker (or this one) retries
// with a different peer.
static void download_worker(const string& filename, int fd,
                            const vector<string>& piece_hash_list,
                            DownloadState& state) {
    thread_local mt19937 rng(random_device{}());

    while (!state.fatal_error) {
        int    chunk_idx = -1;
        string selected_peer;

        {
            lock_guard<mutex> lock(state.mtx);
            if (state.pending.empty()) break;

            chunk_idx = state.pending.front();
            state.pending.pop();

            auto& peers = state.chunk_availability[chunk_idx];
            if (peers.empty()) {
                state.fatal_error = true;
                return;
            }
            uniform_int_distribution<size_t> dist(0, peers.size() - 1);
            selected_peer = peers[dist(rng)];
        }

        const string& expected_hash = (chunk_idx < (int)piece_hash_list.size())
                                       ? piece_hash_list[chunk_idx] : "";

        if (download_chunk_to_fd(selected_peer, filename, chunk_idx, fd, expected_hash)) {
            int done = ++state.chunks_done;
            lock_guard<mutex> lock(log_mutex);
            cout << "\rProgress: " << done << "/" << state.num_chunks
                 << " chunks (" << (done * 100 / state.num_chunks) << "%)" << flush;
        } else {
            lock_guard<mutex> lock(state.mtx);
            auto& peers = state.chunk_availability[chunk_idx];
            peers.erase(remove(peers.begin(), peers.end(), selected_peer), peers.end());
            if (peers.empty()) {
                log_error("Chunk " + to_string(chunk_idx) + " has no remaining peers");
                state.fatal_error = true;
            } else {
                state.pending.push(chunk_idx);  // retry with a different peer
            }
        }
    }
}

/**
 * Download file from peers using parallel chunk download.
 * Phase 1: bitvector queries run concurrently (one future per peer).
 * Phase 2: up to MAX_PARALLEL_DOWNLOADS worker threads drain the chunk queue.
 */
bool download_file_from_peers(const string& filename, const string& dest_path,
                               const vector<string>& seeders, long long filesize,
                               const vector<string>& piece_hash_list) {
    int num_chunks = (filesize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    cout << "Downloading " << filename << " (" << filesize << " bytes, "
         << num_chunks << " chunks)" << endl;

    // --- Phase 1: fetch bitvectors from all peers in parallel ---
    map<int, vector<string>> chunk_availability;
    {
        vector<pair<string, future<string>>> bv_futures;
        bv_futures.reserve(seeders.size());
        for (const string& peer : seeders) {
            bv_futures.emplace_back(peer,
                async(launch::async, get_peer_bitvector, peer, filename));
        }
        for (auto& [peer, fut] : bv_futures) {
            string bitvector = fut.get();
            for (size_t i = 0; i < bitvector.size() && i < (size_t)num_chunks; i++) {
                if (bitvector[i] == '1')
                    chunk_availability[i].push_back(peer);
            }
        }
    }

    // Ensure every chunk has at least one source before we start
    for (int i = 0; i < num_chunks; i++) {
        if (chunk_availability[i].empty()) {
            log_error("Chunk " + to_string(i) + " not available from any peer");
            return false;
        }
    }

    // --- Pre-allocate destination file ---
    {
        ofstream file(dest_path, ios::binary);
        if (!file.is_open()) {
            log_error("Cannot create destination file: " + dest_path);
            return false;
        }
        if (filesize > 0) {
            file.seekp(filesize - 1);
            file.write("\0", 1);
        }
    }

    // Open a raw fd so workers can use pwrite() without serialising on a seek
    int fd = open(dest_path.c_str(), O_RDWR);
    if (fd < 0) {
        perror("open destination file");
        return false;
    }

    // --- Phase 2: parallel chunk download ---
    DownloadState state;
    state.num_chunks         = num_chunks;
    state.chunk_availability = chunk_availability;
    for (int i = 0; i < num_chunks; i++) state.pending.push(i);

    int num_workers = min({(int)seeders.size(), MAX_PARALLEL_DOWNLOADS, num_chunks});
    cout << "Launching " << num_workers << " parallel worker(s)..." << endl;

    vector<future<void>> futures;
    futures.reserve(num_workers);
    for (int i = 0; i < num_workers; i++) {
        futures.push_back(async(launch::async, download_worker,
                                cref(filename), fd,
                                cref(piece_hash_list), ref(state)));
    }
    for (auto& f : futures) f.get();  // wait for all workers

    close(fd);
    cout << endl;  // newline after the \r progress line

    if (state.fatal_error || state.chunks_done.load() < num_chunks) {
        log_error("Download incomplete: " +
                  to_string(state.chunks_done.load()) + "/" + to_string(num_chunks));
        return false;
    }

    cout << "Download complete!" << endl;

    // Mark every chunk as owned so our peer server can serve this file immediately
    {
        lock_guard<mutex> lock(file_mutex);
        shared_files[filename] = dest_path;
        file_chunks[filename]  = vector<bool>(num_chunks, true);
        file_hashes[filename]  = piece_hash_list;
    }

    return true;
}

// ==================== TRACKER COMMUNICATION ====================

/**
 * Send command to tracker and get response
 */
string send_to_tracker(int tracker_socket, const string& command) {
    if (send_data(tracker_socket, command) < 0) {
        return "ERROR$$Failed to send to tracker";
    }
    
    string response = recv_data(tracker_socket);
    if (response.empty()) {
        return "ERROR$$No response from tracker";
    }
    
    return response;
}

/**
 * Parse tracker response
 * Returns: pair<status, message>
 */
pair<string, string> parse_response(const string& response) {
    vector<string> parts = split_string(response);
    if (parts.size() >= 2) {
        return {parts[0], parts[1]};
    }
    return {"ERROR", response};
}

/**
 * Handle upload_file command
 */
void handle_upload(int tracker_socket, const string& filepath, const string& group_id) {
    // Check file exists
    if (!path_exists(filepath)) {
        cout << "Error: File not found: " << filepath << endl;
        return;
    }
    
    string filename = get_filename(filepath);
    long long filesize = get_file_size(filepath);
    
    // Send command to tracker
    string cmd = "upload_file " + filepath + " " + group_id;
    string response = send_to_tracker(tracker_socket, cmd);
    
    auto [status, message] = parse_response(response);
    
    if (status == "ERROR:") {
        cout << "Error: " << message << endl;
        return;
    }
    
    if (message == "SEND_FILE_DETAILS") {
        // Calculate hashes
        cout << "Calculating file hashes..." << endl;
        vector<string> piece_hash_list = calculate_piece_hashes(filepath);
        string piecewise_hash = join_string(piece_hash_list, ",");
        string filehash = sha256_hash(piecewise_hash);
        
        // Send file details: filename$$filesize$$filehash$$piecehashes
        string details = filename + DELIMITER + 
                        to_string(filesize) + DELIMITER + 
                        filehash + DELIMITER + 
                        piecewise_hash;
        
        send_data(tracker_socket, details);
        
        // Get final response
        response = recv_data(tracker_socket);
        auto [final_status, final_msg] = parse_response(response);
        
        if (final_status == "OK") {
            cout << "File uploaded successfully!" << endl;
            
            // Update our state
            lock_guard<mutex> lock(file_mutex);
            shared_files[filename] = filepath;
            
            int num_chunks = (filesize + CHUNK_SIZE - 1) / CHUNK_SIZE;
            file_chunks[filename] = vector<bool>(num_chunks, true);
            file_hashes[filename] = piece_hash_list;
        } else {
            cout << "Error: " << final_msg << endl;
        }
    } else {
        cout << status << " " << message << endl;
    }
}

/**
 * Handle download_file command
 */
void handle_download(int tracker_socket, const string& group_id, 
                     const string& filename, const string& dest_path) {
    // Check destination directory exists
    if (!is_directory(dest_path)) {
        cout << "Error: Destination directory not found: " << dest_path << endl;
        return;
    }
    
    string full_dest_path = dest_path + "/" + filename;
    
    // Check if file already exists
    if (path_exists(full_dest_path)) {
        cout << "Error: File already exists at destination" << endl;
        return;
    }
    
    // Send command to tracker
    string cmd = "download_file " + group_id + " " + filename + " " + dest_path;
    string response = send_to_tracker(tracker_socket, cmd);
    
    auto [status, message] = parse_response(response);
    
    if (status == "ERROR:") {
        cout << "Error: " << message << endl;
        return;
    }
    
    // Parse seeder list response
    // Format: SEEDER_LIST$$seeder1,seeder2,...$$filesize$$piecehashes$$filehash
    vector<string> parts = split_string(response);

    if (parts.size() < 6 || parts[1] != "SEEDER_LIST") {
        cout << "Error: Invalid response from tracker" << endl;
        return;
    }

    vector<string> seeders = split_string(parts[2], ",");
    long long filesize = stoll(parts[3]);
    vector<string> piece_hash_list = split_string(parts[4], ",");
    string expected_filehash = parts[5];

    cout << "Found " << seeders.size() << " seeder(s)" << endl;

    // Download from peers
    if (download_file_from_peers(filename, full_dest_path, seeders, filesize, piece_hash_list)) {
        // Verify whole-file integrity: sha256 of the joined piece hashes must match
        string actual_filehash = sha256_hash(join_string(piece_hash_list, ","));
        if (actual_filehash != expected_filehash) {
            cout << "Error: File integrity check failed (hash mismatch). Deleting." << endl;
            remove(full_dest_path.c_str());
            return;
        }

        // Download verified — now register with the tracker as a seeder
        string reg_response = send_to_tracker(tracker_socket,
                                              "register_seeder " + group_id + " " + filename);
        auto [reg_status, reg_msg] = parse_response(reg_response);
        if (reg_status != "OK") {
            log_error("Warning: could not register as seeder: " + reg_msg);
        }

        downloaded_files[filename] = group_id;
        cout << "File saved to: " << full_dest_path << endl;
    } else {
        cout << "Download failed" << endl;
        // Clean up partial file
        remove(full_dest_path.c_str());
    }
}

/**
 * Show downloads status
 */
void show_downloads() {
    if (downloaded_files.empty()) {
        cout << "No downloads yet" << endl;
        return;
    }
    
    cout << "Downloaded files:" << endl;
    for (const auto& [filename, group] : downloaded_files) {
        cout << "  [" << group << "] " << filename << endl;
    }
}

// ==================== MAIN CLIENT LOOP ====================

void print_help() {
    cout << "\n=== P2P File Sharing Client ===" << endl;
    cout << "Commands:" << endl;
    cout << "  create_user <username> <password>" << endl;
    cout << "  login <username> <password>" << endl;
    cout << "  logout" << endl;
    cout << "  create_group <group_id>" << endl;
    cout << "  join_group <group_id>" << endl;
    cout << "  leave_group <group_id>" << endl;
    cout << "  list_groups" << endl;
    cout << "  list_requests <group_id>" << endl;
    cout << "  accept_request <group_id> <user_id>" << endl;
    cout << "  change_admin <group_id> <new_admin_id>" << endl;
    cout << "  list_files <group_id>" << endl;
    cout << "  upload_file <filepath> <group_id>" << endl;
    cout << "  download_file <group_id> <filename> <dest_path>" << endl;
    cout << "  stop_share <group_id> <filename>" << endl;
    cout << "  show_downloads" << endl;
    cout << "  help" << endl;
    cout << "  quit" << endl;
    cout << endl;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " <client_ip> <client_port> <tracker_ip> <tracker_port>" << endl;
        cerr << "Example: " << argv[0] << " 192.168.1.101 6000 192.168.1.100 5000" << endl;
        return 1;
    }
    
    my_ip = argv[1];
    my_port = stoi(argv[2]);
    tracker_ip = argv[3];
    tracker_port = stoi(argv[4]);
    
    log_info("Starting P2P Client...");
    log_info("Client: " + my_ip + ":" + to_string(my_port));
    log_info("Tracker: " + tracker_ip + ":" + to_string(tracker_port));
    
    // Start peer server in background
    thread server_thread(run_peer_server);
    server_thread.detach();
    
    // Connect to tracker
    int tracker_socket = create_socket();
    if (tracker_socket < 0) {
        return 1;
    }
    
    if (connect_to_server(tracker_socket, tracker_ip, tracker_port) < 0) {
        log_error("Failed to connect to tracker");
        close(tracker_socket);
        return 1;
    }
    
    log_info("Connected to tracker");
    print_help();
    
    // Main command loop
    string line;
    while (true) {
        cout << ">> ";
        cout.flush();
        
        if (!getline(cin, line)) break;
        
        line = trim(line);
        if (line.empty()) continue;
        
        // Parse command
        istringstream iss(line);
        vector<string> args;
        string word;
        while (iss >> word) {
            args.push_back(word);
        }
        
        if (args.empty()) continue;
        
        string command = args[0];
        
        // Handle local commands
        if (command == "quit" || command == "exit") {
            break;
        }
        else if (command == "help") {
            print_help();
            continue;
        }
        else if (command == "show_downloads") {
            show_downloads();
            continue;
        }
        
        // Check login for protected commands
        if (!is_logged_in && command != "create_user" && command != "login") {
            cout << "Please login first" << endl;
            continue;
        }
        
        // Already logged in check
        if (is_logged_in && command == "login") {
            cout << "Already logged in as " << current_user << endl;
            continue;
        }
        
        // Handle special commands
        if (command == "upload_file") {
            if (args.size() != 3) {
                cout << "Usage: upload_file <filepath> <group_id>" << endl;
                continue;
            }
            handle_upload(tracker_socket, args[1], args[2]);
            continue;
        }
        else if (command == "download_file") {
            if (args.size() != 4) {
                cout << "Usage: download_file <group_id> <filename> <dest_path>" << endl;
                continue;
            }
            handle_download(tracker_socket, args[1], args[2], args[3]);
            continue;
        }
        
        // Send command to tracker
        string response = send_to_tracker(tracker_socket, line);
        auto [status, message] = parse_response(response);
        
        // Handle login specially
        if (command == "login" && message == "LOGIN_OK") {
            // Send our address to tracker
            send_data(tracker_socket, my_ip + ":" + to_string(my_port));
            
            // Get final response
            response = recv_data(tracker_socket);
            auto [final_status, final_msg] = parse_response(response);
            
            if (final_status == "OK") {
                is_logged_in = true;
                current_user = args[1];
                cout << "Login successful! Welcome, " << current_user << endl;
            } else {
                cout << "Login failed: " << final_msg << endl;
            }
            continue;
        }
        
        // Handle logout
        if (command == "logout" && status == "OK") {
            is_logged_in = false;
            current_user.clear();
        }
        
        // Handle stop_share locally
        if (command == "stop_share" && status == "OK" && args.size() >= 3) {
            lock_guard<mutex> lock(file_mutex);
            string filename = args[2];
            shared_files.erase(filename);
            file_chunks.erase(filename);
        }
        
        // Print response
        if (status == "OK") {
            if (command == "list_groups" || command == "list_files" || command == "list_requests") {
                // Parse comma-separated list
                vector<string> items = split_string(message, ",");
                for (const string& item : items) {
                    cout << "  " << item << endl;
                }
            } else {
                cout << message << endl;
            }
        } else {
            cout << "Error: " << message << endl;
        }
    }
    
    // Cleanup
    server_running = false;
    close(tracker_socket);
    
    log_info("Client shutdown");
    return 0;
}
