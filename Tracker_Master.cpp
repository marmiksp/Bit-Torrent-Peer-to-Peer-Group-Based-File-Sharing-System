/**
 * Tracker Server for P2P File Sharing System
 * 
 * Responsibilities:
 * - User registration and authentication
 * - Group management (create, join, leave)
 * - File metadata storage (seeders, hashes, sizes)
 * - Providing seeder lists to downloaders
 * 
 * Usage: ./tracker <tracker_ip> <tracker_port>
 * Example: ./tracker 192.168.1.100 5000
 */

#include "common.h"

// ==================== DATA STRUCTURES ====================

// User credentials: username -> password
map<string, string> users;

// User login status: username -> is_logged_in
map<string, bool> user_logged_in;

// User network address: username -> ip:port
map<string, string> user_address;

// Group admin: group_id -> admin_username
map<string, string> group_admin;

// Group members: group_id -> set of usernames
map<string, set<string>> group_members;

// Pending join requests: group_id -> set of usernames
map<string, set<string>> group_pending;

// File seeders: group_id -> (filename -> set of usernames)
map<string, map<string, set<string>>> file_seeders;

// File metadata: filename -> size
map<string, string> file_sizes;

// Piece hashes: filename -> concatenated hashes
map<string, string> piece_hashes;

// Whole-file hash: filename -> sha256(all_piece_hashes)
map<string, string> file_hashes;

// Mutex for thread-safe operations
mutex data_mutex;

// ==================== MESSAGE PROTOCOL ====================

// Response codes
const string OK = "OK";
const string ERROR_PREFIX = "ERROR:";

string make_response(const string& status, const string& message) {
    return status + DELIMITER + message;
}

string make_error(const string& message) {
    return make_response(ERROR_PREFIX, message);
}

string make_success(const string& message) {
    return make_response(OK, message);
}

// ==================== USER MANAGEMENT ====================

string handle_create_user(const vector<string>& args) {
    if (args.size() != 3) {
        return make_error("Usage: create_user <username> <password>");
    }
    
    string username = args[1];
    string password = args[2];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (users.find(username) != users.end()) {
        return make_error("User already exists");
    }
    
    users[username] = password;
    user_logged_in[username] = false;
    
    log_info("User created: " + username);
    return make_success("Account created successfully");
}

string handle_login(const vector<string>& args, string& client_username, int client_socket) {
    if (args.size() != 3) {
        return make_error("Usage: login <username> <password>");
    }
    
    string username = args[1];
    string password = args[2];
    
    lock_guard<mutex> lock(data_mutex);
    
    // Check credentials
    if (users.find(username) == users.end() || users[username] != password) {
        return make_error("Invalid username or password");
    }
    
    // Check if already logged in
    if (user_logged_in[username]) {
        return make_error("User already logged in from another session");
    }
    
    // Mark as logged in
    user_logged_in[username] = true;
    client_username = username;
    
    log_info("User logged in: " + username);
    return make_success("LOGIN_OK");  // Special marker for client to send address
}

string handle_logout(const string& username) {
    if (username.empty()) {
        return make_error("Not logged in");
    }
    
    lock_guard<mutex> lock(data_mutex);
    user_logged_in[username] = false;
    
    log_info("User logged out: " + username);
    return make_success("Logged out successfully");
}

// ==================== GROUP MANAGEMENT ====================

string handle_create_group(const vector<string>& args, const string& username) {
    if (args.size() != 2) {
        return make_error("Usage: create_group <group_id>");
    }
    
    string group_id = args[1];
     
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) != group_admin.end()) {
        return make_error("Group already exists");
    }
    
    group_admin[group_id] = username;
    group_members[group_id].insert(username);
    
    log_info("Group created: " + group_id + " by " + username);
    return make_success("Group created successfully");
}

string handle_join_group(const vector<string>& args, const string& username) {
    if (args.size() != 2) {
        return make_error("Usage: join_group <group_id>");
    }
    
    string group_id = args[1];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (group_members[group_id].count(username)) {
        return make_error("Already a member of this group");
    }
    
    if (group_pending[group_id].count(username)) {
        return make_error("Join request already pending");
    }
    
    group_pending[group_id].insert(username);
    
    log_info("Join request: " + username + " -> " + group_id);
    return make_success("Join request sent");
}

string handle_leave_group(const vector<string>& args, const string& username) {
    if (args.size() != 2) {
        return make_error("Usage: leave_group <group_id>");
    }
    
    string group_id = args[1];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (!group_members[group_id].count(username)) {
        return make_error("Not a member of this group");
    }
    
    if (group_admin[group_id] == username) {
        return make_error("Admin cannot leave. Transfer admin rights first using change_admin");
    }
    
    group_members[group_id].erase(username);
    
    // Remove user from seeder lists in this group
    for (auto& [filename, seeders] : file_seeders[group_id]) {
        seeders.erase(username);
    }
    
    log_info("User left group: " + username + " <- " + group_id);
    return make_success("Left group successfully");
}

string handle_list_groups(const vector<string>& args) {
    if (args.size() != 1) {
        return make_error("Usage: list_groups");
    }
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.empty()) {
        return make_success("No groups found");
    }
    
    vector<string> groups;
    for (const auto& [gid, admin] : group_admin) {
        groups.push_back(gid);
    }
    
    return make_success(join_string(groups, ","));
}

string handle_list_requests(const vector<string>& args, const string& username) {
    if (args.size() != 2) {
        return make_error("Usage: list_requests <group_id>");
    }
    
    string group_id = args[1];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (group_admin[group_id] != username) {
        return make_error("Only admin can view pending requests");
    }
    
    if (group_pending[group_id].empty()) {
        return make_success("No pending requests");
    }
    
    vector<string> requests(group_pending[group_id].begin(), group_pending[group_id].end());
    return make_success(join_string(requests, ","));
}

string handle_accept_request(const vector<string>& args, const string& username) {
    if (args.size() != 3) {
        return make_error("Usage: accept_request <group_id> <user_id>");
    }
    
    string group_id = args[1];
    string user_id = args[2];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (group_admin[group_id] != username) {
        return make_error("Only admin can accept requests");
    }
    
    if (!group_pending[group_id].count(user_id)) {
        return make_error("No pending request from this user");
    }
    
    group_pending[group_id].erase(user_id);
    group_members[group_id].insert(user_id);
    
    log_info("Request accepted: " + user_id + " -> " + group_id);
    return make_success("Request accepted");
}

string handle_change_admin(const vector<string>& args, const string& username) {
    if (args.size() != 3) {
        return make_error("Usage: change_admin <group_id> <new_admin_id>");
    }
    
    string group_id = args[1];
    string new_admin = args[2];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (group_admin[group_id] != username) {
        return make_error("Only current admin can transfer admin rights");
    }
    
    if (!group_members[group_id].count(new_admin)) {
        return make_error("New admin must be a member of the group");
    }
    
    group_admin[group_id] = new_admin;
    
    log_info("Admin changed: " + group_id + " -> " + new_admin);
    return make_success("Admin changed successfully");
}

// ==================== FILE OPERATIONS ====================

string handle_list_files(const vector<string>& args, const string& username) {
    if (args.size() != 2) {
        return make_error("Usage: list_files <group_id>");
    }
    
    string group_id = args[1];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (!group_members[group_id].count(username)) {
        return make_error("Not a member of this group");
    }
    
    if (file_seeders[group_id].empty()) {
        return make_success("No files shared");
    }
    
    vector<string> files;
    for (const auto& [filename, seeders] : file_seeders[group_id]) {
        if (!seeders.empty()) {
            files.push_back(filename);
        }
    }
    
    if (files.empty()) {
        return make_success("No files shared");
    }
    
    return make_success(join_string(files, ","));
}

string handle_upload_file(const vector<string>& args, const string& username, int client_socket) {
    if (args.size() != 3) {
        return make_error("Usage: upload_file <filepath> <group_id>");
    }
    
    string filepath = args[1];
    string group_id = args[2];
    
    {
        lock_guard<mutex> lock(data_mutex);
        
        if (group_admin.find(group_id) == group_admin.end()) {
            return make_error("Group does not exist");
        }
        
        if (!group_members[group_id].count(username)) {
            return make_error("Not a member of this group");
        }
    }
    
    // Request file details from client
    send_data(client_socket, make_success("SEND_FILE_DETAILS"));
    
    // Receive file details: filename$$filesize$$filehash$$piecehashes
    string file_details = recv_data(client_socket);
    if (file_details.empty()) {
        return make_error("Failed to receive file details");
    }
    
    vector<string> parts = split_string(file_details);
    if (parts.size() < 4) {
        return make_error("Invalid file details format");
    }
    
    string filename = parts[0];
    string filesize = parts[1];
    string filehash = parts[2];
    string piecewise_hash = parts[3];
    
    {
        lock_guard<mutex> lock(data_mutex);
        
        file_seeders[group_id][filename].insert(username);
        file_sizes[filename] = filesize;
        piece_hashes[filename] = piecewise_hash;
        file_hashes[filename] = filehash;
    }
    
    log_info("File uploaded: " + filename + " by " + username + " to group " + group_id);
    return make_success("File uploaded successfully");
}

string handle_download_file(const vector<string>& args, const string& username, int client_socket) {
    if (args.size() != 4) {
        return make_error("Usage: download_file <group_id> <filename> <dest_path>");
    }
    
    string group_id = args[1];
    string filename = args[2];
    string dest_path = args[3];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (!group_members[group_id].count(username)) {
        return make_error("Not a member of this group");
    }
    
    if (file_seeders[group_id].find(filename) == file_seeders[group_id].end()) {
        return make_error("File not found in group");
    }
    
    // Get list of active seeders
    vector<string> active_seeders;
    for (const string& seeder : file_seeders[group_id][filename]) {
        if (user_logged_in[seeder] && user_address.count(seeder)) {
            active_seeders.push_back(user_address[seeder]);
        }
    }
    
    if (active_seeders.empty()) {
        return make_error("No active seeders available");
    }

    // Format: SEEDER_LIST$$seeder1,seeder2,...$$filesize$$piecehashes$$filehash
    // NOTE: the downloader is NOT added as a seeder here. They must call
    // register_seeder after confirming the download completed successfully.
    string seeder_list = join_string(active_seeders, ",");
    string response = "SEEDER_LIST" + DELIMITER +
                      seeder_list + DELIMITER +
                      file_sizes[filename] + DELIMITER +
                      piece_hashes[filename] + DELIMITER +
                      file_hashes[filename];
    
    log_info("Download initiated: " + filename + " by " + username);
    return make_success(response);
}

string handle_register_seeder(const vector<string>& args, const string& username) {
    if (args.size() != 3) {
        return make_error("Usage: register_seeder <group_id> <filename>");
    }

    string group_id = args[1];
    string filename = args[2];

    lock_guard<mutex> lock(data_mutex);

    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }

    if (!group_members[group_id].count(username)) {
        return make_error("Not a member of this group");
    }

    if (file_seeders[group_id].find(filename) == file_seeders[group_id].end()) {
        return make_error("File not found in group");
    }

    file_seeders[group_id][filename].insert(username);

    log_info("Seeder registered: " + username + " for " + filename + " in " + group_id);
    return make_success("Registered as seeder");
}

string handle_stop_share(const vector<string>& args, const string& username) {
    if (args.size() != 3) {
        return make_error("Usage: stop_share <group_id> <filename>");
    }
    
    string group_id = args[1];
    string filename = args[2];
    
    lock_guard<mutex> lock(data_mutex);
    
    if (group_admin.find(group_id) == group_admin.end()) {
        return make_error("Group does not exist");
    }
    
    if (file_seeders[group_id].find(filename) == file_seeders[group_id].end()) {
        return make_error("File not found");
    }
    
    file_seeders[group_id][filename].erase(username);
    
    // Clean up if no seeders left
    if (file_seeders[group_id][filename].empty()) {
        file_seeders[group_id].erase(filename);
        file_sizes.erase(filename);
        piece_hashes.erase(filename);
        file_hashes.erase(filename);
    }
    
    log_info("Stop sharing: " + filename + " by " + username);
    return make_success("Stopped sharing file");
}

// ==================== CLIENT HANDLER ====================

void handle_client(int client_socket) {
    string client_username;
    
    log_info("Client connected: socket " + to_string(client_socket));
    
    while (true) {
        // Receive command from client
        string message = recv_data(client_socket);
        
        if (message.empty()) {
            log_info("Client disconnected: " + (client_username.empty() ? "unknown" : client_username));
            break;
        }
        
        // Parse command
        istringstream iss(message);
        vector<string> args;
        string word;
        while (iss >> word) {
            args.push_back(word);
        }
        
        if (args.empty()) continue;
        
        string command = args[0];
        string response;
        
        // Handle commands
        if (command == "create_user") {
            response = handle_create_user(args);
        }
        else if (command == "login") {
            response = handle_login(args, client_username, client_socket);
            
            // If login successful, receive client address
            if (response.find("LOGIN_OK") != string::npos) {
                send_data(client_socket, response);
                string address = recv_data(client_socket);
                if (!address.empty()) {
                    lock_guard<mutex> lock(data_mutex);
                    user_address[client_username] = address;
                    log_info("User address registered: " + client_username + " -> " + address);
                }
                response = make_success("Login successful");
            }
        }
        else if (command == "logout") {
            response = handle_logout(client_username);
            client_username.clear();
        }
        else if (client_username.empty()) {
            response = make_error("Please login first");
        }
        else if (command == "create_group") {
            response = handle_create_group(args, client_username);
        }
        else if (command == "join_group") {
            response = handle_join_group(args, client_username);
        }
        else if (command == "leave_group") {
            response = handle_leave_group(args, client_username);
        }
        else if (command == "list_groups") {
            response = handle_list_groups(args);
        }
        else if (command == "list_requests") {
            response = handle_list_requests(args, client_username);
        }
        else if (command == "accept_request") {
            response = handle_accept_request(args, client_username);
        }
        else if (command == "change_admin") {
            response = handle_change_admin(args, client_username);
        }
        else if (command == "list_files") {
            response = handle_list_files(args, client_username);
        }
        else if (command == "upload_file") {
            response = handle_upload_file(args, client_username, client_socket);
        }
        else if (command == "download_file") {
            response = handle_download_file(args, client_username, client_socket);
        }
        else if (command == "register_seeder") {
            response = handle_register_seeder(args, client_username);
        }
        else if (command == "stop_share") {
            response = handle_stop_share(args, client_username);
        }
        else {
            response = make_error("Unknown command: " + command);
        }
        
        // Send response
        send_data(client_socket, response);
    }
    
    // Cleanup on disconnect
    if (!client_username.empty()) {
        lock_guard<mutex> lock(data_mutex);
        user_logged_in[client_username] = false;
    }
    
    close(client_socket);
}

// ==================== MAIN ====================

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <tracker_ip> <tracker_port>" << endl;
        cerr << "Example: " << argv[0] << " 192.168.1.100 5000" << endl;
        return 1;
    }
    
    string tracker_ip = argv[1];
    int tracker_port = stoi(argv[2]);
    
    log_info("Starting Tracker Server...");
    log_info("IP: " + tracker_ip + ", Port: " + to_string(tracker_port));
    
    // Create server socket
    int server_socket = create_socket();
    if (server_socket < 0) {
        return 1;
    }
    
    // Bind and listen
    if (bind_and_listen(server_socket, tracker_ip, tracker_port) < 0) {
        close(server_socket);
        return 1;
    }
    
    log_info("Tracker is listening for connections...");
    log_info("Type 'quit' to stop the server");
    
    // Start quit detection thread
    thread quit_thread([]() {
        string input;
        while (getline(cin, input)) {
            if (input == "quit") {
                log_info("Shutting down tracker...");
                exit(0);
            }
        }
    });
    quit_thread.detach();
    
    // Accept connections
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Handle client in new thread
        thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }
    
    close(server_socket);
    return 0;
}
