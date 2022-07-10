#include <bits/stdc++.h>
#include <openssl/sha.h>
#include <sys/socket.h> 
#include <sys/types.h> 
#include <signal.h> 
#include <string.h> 
#include <unistd.h> 
#include <arpa/inet.h> 
#include <stdarg.h> 
#include <errno.h> 
#include <fcntl.h>
#include <sys/time.h> 
#include <sys/ioctl.h> 
#include <netdb.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
using namespace std; 

#define TRACKER_PORT 18000
#define ll long long int
#define MAXLINE 4096 
#define SA struct sockaddr 


// ###########################  Local Database ##############################
 
string log_file_name, tracker1_ip, tracker2_ip, cur_tracker_ip, seeder_file_name;
uint16_t tracker1_port, tracker2_port, cur_tracker_port;
unordered_map<string, string> uid_to_ip_port;

unordered_map<string, string> login_cred;
unordered_map<string, bool> is_logged_in;

unordered_map<string, string> group_admins_uid;
vector<string> all_group_id;
unordered_map<string, set<string>> group_members;
unordered_map<string, set<string>> group_pendng_requests;

unordered_map<string, string> piece_wise_hash; 
unordered_map<string, unordered_map<string, set<string>>> seeder_list; // groupid -> {map of filenames -> peer address}

unordered_map<string, string> file_size;

// ###########################  Local Database End ##############################


// ###########################  Functionalities List ##############################

vector<string> get_trackerfile_Info(char*);


int create_user(vector<string>);
int login_validation(vector<string>);


void list_files(vector<string>, int);
void stop_share(vector<string>, int, string);
void leave_group(vector<string>, int, string);
void accept_request(vector<string>, int, string);
void list_requests(vector<string>, int, string);
void join_group(vector<string>, int, string);
void list_groups(vector<string>, int);
int create_group(vector<string>, int, string);


void downloadFile(vector<string>, int, string);
void uploadFile(vector<string>, int, string);


void clear_log();
void write_log(const string &);
bool is_path_exists(const string &s);
vector<string> split_string(string, string);
void* check_input(void*);


void process_args(int, char **);


void handle_connection(int);

// ###########################  Functionalities List End ##############################


// ###########################  Functionalities Implementation ##############################

vector<string> get_trackerfile_Info(char* path){
    fstream trackerInfoFile;
    trackerInfoFile.open(path, ios::in);

    vector<string> res;
    if(trackerInfoFile.is_open()){
        string t;
        while(getline(trackerInfoFile, t)){
            res.push_back(t);
        }
        trackerInfoFile.close();
    }
    else{
        cout << "Tracker Info file not found.\n";
        exit(-1);
    }
    return res;
}

// ********************************************************************
// ********************************************************************


int create_user(vector<string> inpt){
    string user_id = inpt[1];
    string passwd = inpt[2];

    if(login_cred.find(user_id) == login_cred.end()){
        login_cred.insert({user_id, passwd});
    }
    else{
        return -1;
    }
    return 0;
}

int login_validation(vector<string> inpt){
    string user_id = inpt[1];
    string passwd = inpt[2];

    if(login_cred.find(user_id) == login_cred.end() || login_cred[user_id] != passwd){
        return -1;
    }

    if(is_logged_in.find(user_id) == is_logged_in.end()){
        is_logged_in.insert({user_id, true});
    }
    else{
        if(is_logged_in[user_id]){
            return 1;
        }
        else{
            is_logged_in[user_id] = true;
        }
    }
    return 0;
}

// ********************************************************************
// ********************************************************************



void list_groups(vector<string> inpt, int client_socket){
    //inpt - [list_groups];
    if(inpt.size() != 1){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "All groups:", 11);

    char dum[5];
    read(client_socket, dum, 5);

    if(all_group_id.size() == 0){
        write(client_socket, "No groups found$$", 18);
        return;
    }

    string reply = "";
    for(size_t i=0; i<all_group_id.size(); i++){
        reply += all_group_id[i] + "$$";
    }
    write(client_socket, &reply[0], reply.length());
}

int create_group(vector<string> inpt, int client_socket, string client_uid){
    //inpt - [create_group gid] 
    if(inpt.size() != 2){
        write(client_socket, "Invalid argument count", 22);
        return -1;
    }
    for(auto i: all_group_id){
        if(i == inpt[1]) return -1;
    }
    group_admins_uid.insert({inpt[1], client_uid});
    all_group_id.push_back(inpt[1]);
    group_members[inpt[1]].insert(client_uid);
    return 0;
}

void join_group(vector<string> inpt, int client_socket, string client_uid){
    //inpt - [join_group gid]
    if(inpt.size() != 2){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write_log("join_group function ..");

    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(group_members[inpt[1]].find(client_uid) == group_members[inpt[1]].end()){
        group_pendng_requests[inpt[1]].insert(client_uid);
        write(client_socket, "Group request sent", 18);
    }
    else{
        write(client_socket, "You are already in this group", 30);
    }
    
}

void list_requests(vector<string> inpt, int client_socket, string client_uid){
    // inpt - [list_requests groupid]
    if(inpt.size() != 2){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "Fetching group requests...", 27);

    char dum[5];
    read(client_socket, dum, 5);

    write_log("hereeee");
    if(group_admins_uid.find(inpt[1])==group_admins_uid.end() || group_admins_uid[inpt[1]] != client_uid){
        write_log("iffff");
        write(client_socket, "**err**", 7);
    }
    else if(group_pendng_requests[inpt[1]].size() == 0){
        write(client_socket, "**er2**", 7);
    }
    else {
        string reply = "";
        write_log("pending request size: "+  to_string(group_pendng_requests[inpt[1]].size()));
        for(auto i = group_pendng_requests[inpt[1]].begin(); i!= group_pendng_requests[inpt[1]].end(); i++){
            reply += string(*i) + "$$";
        }
        write(client_socket, &reply[0], reply.length());
        write_log("reply :" + reply);
    }
}

void accept_request(vector<string> inpt, int client_socket, string client_uid){
    // inpt - [accept_request groupid user_id]
    if(inpt.size() != 3){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "Accepting request...", 21);

    char dum[5];
    read(client_socket, dum, 5);

    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write_log("inside accept_request if");
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(group_admins_uid.find(inpt[1])->second == client_uid){
        write_log("inside accept_request else if with pending list:");
        for(auto i: group_pendng_requests[inpt[1]]){
            write_log(i);
        }
        group_pendng_requests[inpt[1]].erase(inpt[2]);
        group_members[inpt[1]].insert(inpt[2]);
        write(client_socket, "Request accepted.", 18);
    }
    else{
        write_log("inside accept_request else");
        //cout << group_admins_uid.find(inpt[1])->second << " " << client_uid <<  endl;
        write(client_socket, "You are not the admin of this group", 35);
    }
    
}

void change_admin(vector<string> inpt, int client_socket, string client_uid){
    // inpt - [change_admin groupid new_admin_user_id]
    if(inpt.size() != 3){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "Changing Group Admin...", 21);

    char dum[5];
    read(client_socket, dum, 5);

    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write_log("inside change_admin if");
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(group_admins_uid.find(inpt[1])->second == client_uid){
        // write_log("inside accept_request else if with pending list:");

        if(group_members[inpt[1]].find(inpt[2]) != group_members[inpt[1]].end())
        {
            group_admins_uid[inpt[1]] = inpt[2];
            write(client_socket, "Admin changed.", 14);
        }
        else{
            write(client_socket, "Invalid user ID.", 17);
        }
    }
    else{
        write_log("inside change_admin else");
        //cout << group_admins_uid.find(inpt[1])->second << " " << client_uid <<  endl;
        write(client_socket, "You are not the admin of this group", 35);
    }
    
}


void leave_group(vector<string> inpt, int client_socket, string client_uid){
    // inpt - [leave_group groupid]
    if(inpt.size() != 2){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "Leaving group...", 17);

    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(group_members[inpt[1]].find(client_uid) != group_members[inpt[1]].end()){
        if(group_admins_uid[inpt[1]] == client_uid){
            write(client_socket, "You are the admin of this group, you cant leave!", 48);
        }
        else{
            group_members[inpt[1]].erase(client_uid);
            write(client_socket, "Group left succesfully", 23);
        }
    }
    else{
        write(client_socket, "You are not in this group", 25);
    }
}

void list_files(vector<string> inpt, int client_socket){
    // inpt - list_files​ <group_id>
    if(inpt.size() != 2){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    write(client_socket, "Fetching files...", 17);

    char dum[5];
    read(client_socket, dum, 5);
    write_log("dum read");

    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(seeder_list[inpt[1]].size() == 0){
        write(client_socket, "No files found.", 15);
    }
    else
    {
        write_log("in else of list files");

        string reply = "";

        for(auto i: seeder_list[inpt[1]]){
            reply += i.first + "$$";
        }
        reply = reply.substr(0, reply.length()-2);
        write_log("list of files reply:" + reply);

        write(client_socket, &reply[0], reply.length());
    }
}

void stop_share(vector<string> inpt, int client_socket, string client_uid){
    // inpt - stop_share ​<group_id> <file_name>
    if(inpt.size() != 3){
        write(client_socket, "Invalid argument count", 22);
        return;
    }
    if(group_admins_uid.find(inpt[1]) == group_admins_uid.end()){
        write(client_socket, "Invalid group ID.", 19);
    }
    else if(seeder_list[inpt[1]].find(inpt[2]) == seeder_list[inpt[1]].end()){
        write(client_socket, "File not yet shared in the group", 32);
    }
    else{
        seeder_list[inpt[1]][inpt[2]].erase(client_uid);
        if(seeder_list[inpt[1]][inpt[2]].size() == 0){
            seeder_list[inpt[1]].erase(inpt[2]);
        }
        write(client_socket, "Stopped sharing the file", 25);
    }
}

// ********************************************************************
// ********************************************************************


void uploadFile(vector<string> inpt, int client_socket, string client_uid){
    //inpt - upload_file​ <file_path> <group_id​>
    if(inpt.size() != 3){
        write(client_socket, "Invalid argument count", 22);
    }
    else if(group_members.find(inpt[2]) == group_members.end()){
        write(client_socket, "Error 101:", 10);
    }
    else if(group_members[inpt[2]].find(client_uid) == group_members[inpt[2]].end()){
        write(client_socket, "Error 102:", 10);
    }
    else if(!is_path_exists(inpt[1])){
        write(client_socket, "Error 103:", 10);
    }
    else{
        char fileDetails[524288] =  {0};
        write(client_socket, "Uploading...", 12);
        write_log("uploading");

        if(read(client_socket , fileDetails, 524288)){
            if(string(fileDetails) == "error") return;

            vector<string> fdet = split_string(string(fileDetails), "$$");
            //fdet = [filepath, peer address, file size, file hash, piecewise hash] 
            string filename = split_string(string(fdet[0]), "/").back();

            string hashOfPieces = "";
            for(size_t i=4; i<fdet.size(); i++){
                hashOfPieces += fdet[i];
                if(i != fdet.size()-1) hashOfPieces += "$$";
            }
            
            piece_wise_hash[filename] = hashOfPieces;
            
            if(seeder_list[inpt[2]].find(filename) != seeder_list[inpt[2]].end()){
                seeder_list[inpt[2]][filename].insert(client_uid);
            }
            else{
                seeder_list[inpt[2]].insert({filename, {client_uid}});
            }
            file_size[filename] = fdet[2];
            
            write(client_socket, "Uploaded", 8);
        }
    }
}

void downloadFile(vector<string> inpt, int client_socket, string client_uid){
    // inpt - download_file​ <group_id> <file_name> <destination_path>
    if(inpt.size() != 4){
        write(client_socket, "Invalid argument count", 22);
    }
    else if(group_members.find(inpt[1]) == group_members.end()){
        write(client_socket, "Error 101:", 10);
    }
    else if(group_members[inpt[1]].find(client_uid) == group_members[inpt[1]].end()){
        write(client_socket, "Error 102:", 10);
    }
    else{
        if(!is_path_exists(inpt[3])){
            write(client_socket, "Error 103:", 10);
            return;
        }

        char fileDetails[524288] =  {0};
        // fileDetails = [filename, destination, group id]
        write(client_socket, "Downloading...", 13);

        if(read(client_socket , fileDetails, 524288)){
            vector<string> fdet = split_string(string(fileDetails), "$$");
            
            string reply = "";
            if(seeder_list[inpt[1]].find(fdet[0]) != seeder_list[inpt[1]].end()){
                for(auto i: seeder_list[inpt[1]][fdet[0]]){
                    if(is_logged_in[i]){
                        reply += uid_to_ip_port[i] + "$$";
                    }
                }
                reply += file_size[fdet[0]];
                write_log("seeder list: "+ reply);
                write(client_socket, &reply[0], reply.length());

                char dum[5];
                read(client_socket, dum, 5);
                
                write(client_socket, &piece_wise_hash[fdet[0]][0], piece_wise_hash[fdet[0]].length());
            
                seeder_list[inpt[1]][inpt[2]].insert(client_uid);
            }
            else{
                write(client_socket, "File not found", 14);
            }
        }
        
    }
}


// ********************************************************************
// ********************************************************************


void clear_log(){
    ofstream out;
    out.open(log_file_name);
    out.clear();
    out.close();
}

void write_log(const string &text ){
    ofstream log_file(log_file_name, ios_base::out | ios_base::app );
    log_file << text << endl;
}

bool is_path_exists(const string &s){
  struct stat buffer;
  return (stat (s.c_str(), &buffer) == 0);
}

vector<string> split_string(string str, string delim){
    vector<string> res;

    size_t pos = 0;
    while ((pos = str.find(delim)) != string::npos) {
        string t = str.substr(0, pos);
        res.push_back(t);
        str.erase(0, pos + delim.length());
    }
    res.push_back(str);

    return res;
}

/* Thread function which detects if quit was typed in */
void* check_input(void* arg){
    while(true){
        string inputline;
        getline(cin, inputline);
        if(inputline == "quit"){
            exit(0);
        }
    }
}


// ********************************************************************
// ********************************************************************


void process_args(int argc, char *argv[]){
    log_file_name = "trackerlog" + string(argv[2]) + ".txt";
    clear_log();

    vector<string> trackeraddress = get_trackerfile_Info(argv[1]);
    if(string(argv[2]) == "1"){
        tracker1_ip = trackeraddress[0];
        tracker1_port = stoi(trackeraddress[1]);
        cur_tracker_ip = tracker1_ip;
        cur_tracker_port = tracker1_port;
    }
    else{
        tracker2_ip = trackeraddress[2];
        tracker2_port = stoi(trackeraddress[3]);
        cur_tracker_ip = tracker2_ip;
        cur_tracker_port = tracker2_port;
    }

    write_log("Tracker 1 Address : " + string(tracker1_ip)+ ":" +to_string(tracker1_port));
    write_log("Tracker 2 Address : " + string(tracker2_ip)+ ":" +to_string(tracker2_port));
    write_log("Log file name : " + string(log_file_name) + "\n");
}


// ********************************************************************
// ********************************************************************


//client connection handling thread
void handle_connection(int client_socket){
    string client_uid = "";
    string client_gid = "";
    write_log("***********pthread started for client socket number " + to_string(client_socket));

    //for continuously checking the commands sent by the client
    while(true){
        char inptline[1024] = {0}; 

        if(read(client_socket , inptline, 1024) <=0){
            is_logged_in[client_uid] = false;
            close(client_socket);
            break;
        }
        write_log("client request:" + string(inptline));

        string s, in = string(inptline);
        stringstream ss(in);
        vector<string> inpt;

        while(ss >> s){
            inpt.push_back(s);
        }

        if(inpt[0] == "create_user"){
            if(inpt.size() != 3){
                write(client_socket, "Invalid argument count", 22);
            }
            else{
                if(create_user(inpt) < 0){
                    write(client_socket, "User exists", 11);
                }
                else{
                    write(client_socket, "Account created", 15);
                }
            }
        }
        else if(inpt[0] == "login"){
            if(inpt.size() != 3){
                write(client_socket, "Invalid argument count", 22);
            }
            else{
                int r;
                if((r = login_validation(inpt)) < 0){
                    write(client_socket, "Username/password incorrect", 28);
                }
                else if(r > 0){
                    write(client_socket, "You already have one active session", 35);
                }
                else{
                    write(client_socket, "Login Successful", 16);
                    client_uid = inpt[1];
                    char buf[96];
                    read(client_socket, buf, 96);
                    string peerAddress = string(buf);
                    uid_to_ip_port[client_uid] = peerAddress;
                }
            }            
        }
        else if(inpt[0] ==  "logout"){
            is_logged_in[client_uid] = false;
            write(client_socket, "Logout Successful", 17);
            write_log("logout sucess\n");
        }
        else if(inpt[0] == "upload_file"){
            uploadFile(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "download_file"){
            downloadFile(inpt, client_socket, client_uid);
            write_log("after down");
        }
        else if(inpt[0] == "create_group"){
            if(create_group(inpt, client_socket, client_uid) >=0){
                client_gid = inpt[1];
                write(client_socket, "Group created", 13);
            }
            else{
                write(client_socket, "Group exists", 12);
            }
        }
        else if(inpt[0] == "list_groups"){
            list_groups(inpt, client_socket);
        }
        else if(inpt[0] == "join_group"){
            join_group(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "list_requests"){
            list_requests(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "accept_request"){
            accept_request(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "change_admin"){
            change_admin(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "leave_group"){
            leave_group(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "list_files"){
            list_files(inpt, client_socket);
        }
        else if(inpt[0] == "stop_share"){
            stop_share(inpt, client_socket, client_uid);
        }
        else if(inpt[0] == "show_downloads"){
            write(client_socket, "Loading...", 10);
        }
        else{
            write(client_socket, "Invalid command", 16);
        }
    }
    write_log("***********pthread ended for client socket number " + to_string(client_socket));
    close(client_socket);
}


// ********************************************************************
// ********************************************************************



int main(int argc, char *argv[]){ 

    if(argc != 3){
        cout << "Give arguments as <tracker info file name> and <tracker_number>\n";
        return -1;
    }

    process_args(argc, argv);

    int tracker_socket; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address); 
    pthread_t  exitDetectionThreadId;
       
    if ((tracker_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
    write_log("Tracker socket created.");
       
    if (setsockopt(tracker_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_port = htons(cur_tracker_port); 

    if(inet_pton(AF_INET, &cur_tracker_ip[0], &address.sin_addr)<=0)  { 
        printf("\nInvalid address/ Address not supported \n"); 
        return -1; 
    } 
       
    if (bind(tracker_socket, (SA *)&address,  sizeof(address))<0) { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    write_log("Binding completed.");

    if (listen(tracker_socket, 3) < 0) { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    write_log("Listening...");

    vector<thread> threadVector;

    if(pthread_create(&exitDetectionThreadId, NULL, check_input, NULL) == -1){
        perror("pthread"); 
        exit(EXIT_FAILURE); 
    }

    while(true){
        int client_socket;

        if((client_socket = accept(tracker_socket, (SA *)&address, (socklen_t *)&addrlen)) < 0){
            perror("Acceptance error");
            write_log("Error in accept"); 
        }
        write_log("Connection Accepted");

        threadVector.push_back(thread(handle_connection, client_socket));
    }
    for(auto i=threadVector.begin(); i!=threadVector.end(); i++){
        if(i->joinable()) i->join();
    }

    write_log("EXITING.");
    return 0; 
} 
