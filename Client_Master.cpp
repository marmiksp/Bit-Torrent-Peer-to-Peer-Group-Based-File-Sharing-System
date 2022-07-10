#include <bits/stdc++.h>
#include <openssl/sha.h>
#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
using namespace std;

#define SA struct sockaddr 
#define ll long long int

string peer_ip; 
uint16_t peer_port;
bool is_loggedin; //true if logged in, false otherwise
unordered_map<string, unordered_map<string, bool>> is_uploaded; // group -> filename -> bool
unordered_map<string, vector<int>> file_to_chunk_bitvector; // filename -> bitvector of chunks
vector<vector<string>> curdown_ch_ind_to_seederlist; // currently downloading file chunk's index -> vector of peer's ip:port who has that chunk.
unordered_map<string, string> file_name_to_filePath; // filename -> filepath
vector<string> cur_file_PiecewiseHash; // currently downloading file's piecewise hash
string tracker1_ip;
string tracker2_ip;
uint16_t tracker1_port, tracker2_port;

unordered_map<string, string> downloaded_files; // filename -> groupid
bool is_corrupted_file;
string log_file_name;


typedef struct peerFileDetails{
    string serverPeerIP;
    string filename;
    ll filesize;
} peerFileDetails;
 
typedef struct reqdChunkDetails{
    string serverPeerIP;
    string filename;
    ll chunkNum; 
    string destination;
} reqdChunkDetails;



#define SEGMENT_SIZE 524288
#define SIZE 32768

int list_groups(int);
int list_requests(int);
void accept_request(int);
void leave_group(int);
void show_downloads();
void list_files(int);

void cal_string_hash(string, string&);
string cal_hash(char*);
string cal_file_hash(char*);

long long file_size(char*);
void write_log(const string &);
void clear_log();
vector<string> split_string(string, string);
void update_file_chunk_bitvector(string, ll, ll, bool);

void configure_trackerinfo(int, char **);
int connect_to_tracker(int, struct sockaddr_in &, int);
int process_tracker_command(vector<string>, int);

void handle_peer_request(int);
string connect_to_peer(char*, char*, string);
void* run_as_server(void*);

void send_chunk(char*, int, int);
int write_chunk(int, ll cnkNum, char*);
void get_bit_vector(peerFileDetails* pf);
void getChunk(reqdChunkDetails* reqdChunk);
void piece_selection_algorithm(vector<string>, vector<string>);
int downloadFile(vector<string>, int);
int uploadFile(vector<string>, int);


// ***************** Group Related Functions **************************

 
int list_groups(int sock){
    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);

    char reply[3*SIZE];
    memset(reply, 0, sizeof(reply));
    read(sock, reply, 3*SIZE);
    write_log("list of groups reply: " + string(reply));

    vector<string> grps = split_string(string(reply), "$$");

    for(size_t i=0; i<grps.size()-1; i++){
        cout << grps[i] << endl;
    }
    return 0;
}

int list_requests(int sock){
    write_log("waiting for response");

    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);
    
    char reply[3*SIZE];
    memset(reply, 0, 3*SIZE);
    read(sock, reply, 3*SIZE);
    if(string(reply) == "**err**") return -1;
    if(string(reply) == "**er2**") return 1;
    write_log("request list: " + string(reply));

    vector<string> requests = split_string(string(reply), "$$");
    write_log("list request response size: "+ to_string(requests.size()));
    for(size_t i=0; i<requests.size()-1; i++){
        cout << requests[i] << endl;
    }
    return 0;
}

void accept_request(int sock){
    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);

    char buf[96];
    read(sock, buf, 96);
    cout << buf << endl;
}

void change_admin(int sock){
    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);

    char buf[96];
    read(sock, buf, 96);
    cout << buf << endl;
}

void leave_group(int sock){
    write_log("waiting for response");
    char buf[96];
    read(sock, buf, 96);
    cout << buf << endl;
}

void list_files(int sock){
    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);

    char buf[1024];
    bzero(buf, 1024);
    read(sock, buf, 1024);
    vector<string> listOfFiles = split_string(string(buf), "$$");

    for(auto i: listOfFiles)
        cout << i << endl;
}

void show_downloads(){
    for(auto i: downloaded_files){
        cout << "[C] " << i.second << " " << i.first << endl;
    }
}



//  *************** Hash Related Functions **************************

void cal_string_hash(string segmentString, string& hash){
    unsigned char md[20];
    if(!SHA1(reinterpret_cast<const unsigned char *>(&segmentString[0]), segmentString.length(), md)){
        printf("Error in hashing\n");
    }
    else{
        for(int i=0; i<20; i++){
            char buf[3];
            sprintf(buf, "%02x", md[i]&0xff);
            hash += string(buf);
        }
    }
    hash += "$$";
}

string cal_hash(char* path){
    
    int  i, accum;
    FILE *fp1;

    long long fileSize = file_size(path);
    if(fileSize == -1){
        return "$";
    }
    int segments = fileSize/SEGMENT_SIZE + 1;
    char line[SIZE + 1];
    string hash = "";

    fp1 = fopen(path, "r");

    if(fp1){ 
        for(i=0;i<segments;i++){
            accum = 0;
            string segmentString;

            int rc;
            while(accum < SEGMENT_SIZE && (rc = fread(line, 1, min(SIZE-1, SEGMENT_SIZE-accum), fp1))){
                line[rc] = '\0';
                accum += strlen(line);
                segmentString += line;
                memset(line, 0, sizeof(line));
            }

            cal_string_hash(segmentString, hash);

        }
        
        fclose(fp1);
    }
    else{
        printf("File not found.\n");
    }
    hash.pop_back();
    hash.pop_back();
    return hash;
}

string cal_file_hash(char* path){

    ostringstream buf; 
    ifstream input (path); 
    buf << input.rdbuf(); 
    string contents =  buf.str(), hash;

    unsigned char md[SHA256_DIGEST_LENGTH];
    if(!SHA256(reinterpret_cast<const unsigned char *>(&contents[0]), contents.length(), md)){
        printf("Error in hashing\n");
    }
    else{
        for(int i=0; i<SHA256_DIGEST_LENGTH; i++){
            char buf[3];
            sprintf(buf, "%02x", md[i]&0xff);
            hash += string(buf);
        }
    }
    return hash;
}


// ******************** Utilility Functions *************


void write_log(const string &text ){
    ofstream log_file(log_file_name, ios_base::out | ios_base::app );
    log_file << text << endl;
}

void clear_log(){
    ofstream out;
    out.open(log_file_name);
    out.clear();
    out.close();
}
 
vector<string> split_string(string address, string delim = ":"){
    vector<string> res;

    size_t pos = 0;
    while ((pos = address.find(delim)) != string::npos) {
        string t = address.substr(0, pos);
        res.push_back(t);
        address.erase(0, pos + delim.length());
    }
    res.push_back(address);

    return res;
}

void update_file_chunk_bitvector(string filename, ll l, ll r, bool isUpload){
    if(isUpload){
        vector<int> tmp(r-l+1, 1);
        file_to_chunk_bitvector[filename] = tmp;
    }
    else{
        file_to_chunk_bitvector[filename][l] = 1;
        write_log("chunk vector updated for " + filename + " at " + to_string(l));
    }
}

long long file_size(char *path){
    FILE *fp = fopen(path, "rb"); 

    long size=-1;
    if(fp){
        fseek (fp, 0, SEEK_END);
        size = ftell(fp)+1;
        fclose(fp);
    }
    else{
        printf("File not found.\n");
        return -1;
    }
    return size;
}



// ******************** Tracker Related Functions *************

void configure_trackerinfo(int argc, char *argv[]){
    string peerInfo = argv[1];
    string trackerInfoFilename = argv[2];

    log_file_name = peerInfo + "_log.txt";
    clear_log();

    vector<string> peeraddress = split_string(peerInfo);
    peer_ip = peeraddress[0];
    peer_port = stoi(peeraddress[1]);

    char curDir[128];
    getcwd(curDir, 128);
    
    string path = string(curDir);
    path += "/" + trackerInfoFilename;
    vector<string> trackerInfo;
    
    fstream trackerInfoFile;
    trackerInfoFile.open(&path[0], ios::in);

    if(trackerInfoFile.is_open()){
        string t;
        while(getline(trackerInfoFile, t)){
            trackerInfo.push_back(t);
        }
        trackerInfoFile.close();
    }
    else{
        cout << "Tracker Info file not found.\n";
        exit(-1);
    }
    

    tracker1_ip = trackerInfo[0];
    tracker1_port = stoi(trackerInfo[1]);
    tracker2_ip = trackerInfo[2];
    tracker2_port = stoi(trackerInfo[3]);

    write_log("Peer Address : " + string(peer_ip)+ ":" +to_string(peer_port));
    write_log("Tracker 1 Address : " + string(tracker1_ip)+ ":" +to_string(tracker1_port));
    write_log("Tracker 2 Address : " + string(tracker2_ip)+ ":" +to_string(tracker2_port));
    write_log("Log file name : " + string(log_file_name) + "\n");
}

int connect_to_tracker(int trackerNum, struct sockaddr_in &serv_addr, int sock){
    char* curTrackIP;
    uint16_t curTrackPort;
    if(trackerNum == 1){
        curTrackIP = &tracker1_ip[0]; 
        curTrackPort = tracker1_port;
    }
    else{
        curTrackIP = &tracker2_ip[0]; 
        curTrackPort = tracker2_port;
    }

    bool err = 0;

    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(curTrackPort); 
       
    if(inet_pton(AF_INET, curTrackIP, &serv_addr.sin_addr)<=0)  { 
        err = 1;
    } 
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) { 
        err = 1;
    } 
    if(err){
        if(trackerNum == 1)
            return connect_to_tracker(2, serv_addr, sock);
        else
            return -1;
    }
    write_log("connected to server " + to_string(curTrackPort));
    return 0;
}
 
int process_tracker_command(vector<string> inpt, int sock){
    char server_reply[10240]; 
    bzero(server_reply, 10240);
    read(sock , server_reply, 10240); 
    cout << server_reply << endl;
    write_log("primary server response: " + string(server_reply));
 
    if(string(server_reply) == "Invalid argument count") return 0;
    if(inpt[0] == "login"){
        if(string(server_reply) == "Login Successful"){
            is_loggedin = true;
            string peerAddress = peer_ip + ":" + to_string(peer_port);
            write(sock, &peerAddress[0], peerAddress.length());
        }
    }
    else if(inpt[0] == "logout"){
        is_loggedin = false;
    }
    else if(inpt[0] == "upload_file"){
        if(string(server_reply) == "Error 101:"){
            cout << "Group doesn't exist" << endl;
            return 0;
        }
        else  if(string(server_reply) == "Error 102:"){
            cout << "You are not a member of this group" << endl;
            return 0;
        }
        else  if(string(server_reply) == "Error 103:"){
            cout << "File not found." << endl;
            return 0;
        }
        return uploadFile(inpt, sock);
    }
    else if(inpt[0] == "download_file"){
        if(string(server_reply) == "Error 101:"){
            cout << "Group doesn't exist" << endl;
            return 0;
        }
        else  if(string(server_reply) == "Error 102:"){
            cout << "You are not a member of this group" << endl;
            return 0;
        }
        else  if(string(server_reply) == "Error 103:"){
            cout << "Directory not found" << endl;
            return 0;
        }
        if(downloaded_files.find(inpt[2])!= downloaded_files.end()){
            cout << "File already downloaded" << endl;
            return 0;
        }
        return downloadFile(inpt, sock);
    }
    else if(inpt[0] == "list_groups"){
        return list_groups(sock);
    }
    else if(inpt[0] == "list_requests"){
        int t;
        if((t = list_requests(sock)) < 0){
            cout << "You are not the admin of this group\n";
        }
        else if(t>0){
            cout << "No pending requests\n";
        }
        else return 0;
    }
    else if(inpt[0] == "accept_request"){
        accept_request(sock);
    }
    else if(inpt[0] == "change_admin"){
        change_admin(sock);
    }
    else if(inpt[0] == "leave_group"){
        leave_group(sock);
    }
    else if(inpt[0] == "list_files"){
        list_files(sock);
    }
    else if(inpt[0] == "stop_share"){
        is_uploaded[inpt[1]].erase(inpt[2]);
    }
    else if(inpt[0] == "show_downloads"){
        show_downloads();
    }
    return 0;
}


// ******************* Peer To Peer **************************

/* Handles different requests from peer client */
void handle_peer_request(int client_socket){
    string client_uid = "";

    write_log("\nclient socket num: " + to_string(client_socket) + "\n");
    char inptline[1024] = {0}; 

    if(read(client_socket , inptline, 1024) <=0){
        close(client_socket);
        return;
    }
    
    write_log("client request at server " + string(inptline));
    vector<string> inpt = split_string(string(inptline), "$$");
    write_log(inpt[0]);

    if(inpt[0] == "get_chunk_vector"){
        write_log("\nsending chunk vector..");
        string filename = inpt[1];
        vector<int> chnkvec = file_to_chunk_bitvector[filename];
        string tmp = "";
        for(int i: chnkvec) tmp += to_string(i);
        char* reply = &tmp[0];
        write(client_socket, reply, strlen(reply));
        write_log("sent: " + string(reply));
    }
    else if(inpt[0] == "get_chunk"){
        //inpt = [get_chunk, filename, to_string(chunkNum), destination]
        write_log("\nsending chunk...");
        string filepath = file_name_to_filePath[inpt[1]];
        ll chunkNum = stoll(inpt[2]);
        write_log("filepath: "+ filepath);

        write_log("sending " + to_string(chunkNum) + " from " + string(peer_ip) + ":" + to_string(peer_port));

        send_chunk(&filepath[0], chunkNum, client_socket);
        
    }
    else if(inpt[0] == "get_file_path"){
        string filepath = file_name_to_filePath[inpt[1]];
        write_log("command from peer client: " +  string(inptline));
        write(client_socket, &filepath[0], strlen(filepath.c_str()));
    }
    close(client_socket);
    return;
}

/*Connects to <serverPeerIP:serverPortIP> and sends it <command>*/
string connect_to_peer(char* serverPeerIP, char* serverPortIP, string command){
    int peersock = 0;
    struct sockaddr_in peer_serv_addr; 

    write_log("\nInside connect_to_peer");

    if ((peersock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  
        printf("\n Socket creation error \n"); 
        return "error"; 
    } 
    write_log("Socket Created");

    peer_serv_addr.sin_family = AF_INET; 
    uint16_t peerPort = stoi(string(serverPortIP));
    peer_serv_addr.sin_port = htons(peerPort); 
    write_log("\n needs to connect to " + string(serverPeerIP) + ":" + to_string(peerPort));

    if(inet_pton(AF_INET, serverPeerIP, &peer_serv_addr.sin_addr) < 0){ 
        perror("Peer Connection Error(INET)");
    } 
    if (connect(peersock, (struct sockaddr *)&peer_serv_addr, sizeof(peer_serv_addr)) < 0) { 
        perror("Peer Connection Error");
    } 
    write_log("Connected to peer " + string(serverPeerIP) + ":" + to_string(peerPort));
 
    string curcmd = split_string(command, "$$").front();
    write_log("current command " + curcmd);

    if(curcmd == "get_chunk_vector"){
        if(send(peersock , &command[0] , strlen(&command[0]) , MSG_NOSIGNAL ) == -1){
            printf("Error: %s\n",strerror(errno));
            return "error"; 
        }
        write_log("sent command to peer: " + command);
        char server_reply[10240] = {0};
        if(read(peersock, server_reply, 10240) < 0){
            perror("err: ");
            return "error";
        }
        write_log("got reply: " + string(server_reply));
        close(peersock);
        return string(server_reply);
    }
    else if(curcmd == "get_chunk"){
        //"get_chunk $$ filename $$ to_string(chunkNum) $$ destination
        if(send(peersock , &command[0] , strlen(&command[0]) , MSG_NOSIGNAL ) == -1){
            printf("Error: %s\n",strerror(errno));
            return "error"; 
        }
        write_log("sent command to peer: " + command);
        vector<string> cmdtokens = split_string(command, "$$");
        
        string despath = cmdtokens[3];
        ll chunkNum = stoll(cmdtokens[2]);
        write_log("\ngetting chunk " + to_string(chunkNum) + " from "+ string(serverPortIP));

        write_chunk(peersock, chunkNum, &despath[0]);

        return "ss";
    }
    else if(curcmd == "get_file_path"){
        if(send(peersock , &command[0] , strlen(&command[0]) , MSG_NOSIGNAL ) == -1){
            printf("Error: %s\n",strerror(errno));
            return "error"; 
        }
        char server_reply[10240] = {0};
        if(read(peersock, server_reply, 10240) < 0){
            perror("err: ");
            return "error";
        }
        write_log("server reply for get file path:" + string(server_reply));
        file_name_to_filePath[split_string(command, "$$").back()] = string(server_reply);
    }

    close(peersock);
    write_log("terminating connection with " + string(serverPeerIP) + ":" + to_string(peerPort));
    return "aa";
}

/* The peer acts as a server, continuously listening for connection requests */
void* run_as_server(void* arg){
    int server_socket; 
    struct sockaddr_in address; 
    int addrlen = sizeof(address); 
    int opt = 1; 

    write_log("\n" + to_string(peer_port) + " will start running as server");
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
    write_log(" Server socket created.");

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_port = htons(peer_port); 

    if(inet_pton(AF_INET, &peer_ip[0], &address.sin_addr)<=0)  { 
        printf("\nInvalid address/ Address not supported \n"); 
        return NULL; 
    } 
       
    if (bind(server_socket, (SA *)&address,  sizeof(address))<0) { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    write_log(" Binding completed.");

    if (listen(server_socket, 3) < 0) { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    write_log("Listening...\n");

    vector<thread> vThread;
    while(true){

        int client_socket;

        if((client_socket = accept(server_socket, (SA *)&address, (socklen_t *)&addrlen)) < 0){
            perror("Acceptance error");
            write_log("Error in accept"); 
        }
        write_log(" Connection Accepted");

        vThread.push_back(thread(handle_peer_request, client_socket));
    }
    for(auto it=vThread.begin(); it!=vThread.end();it++){
        if(it->joinable()) it->join();
    }
    close(server_socket);
}




//************************ Upload and Download Functions *******************************//



void send_chunk(char* filepath, int chunkNum, int client_socket){

    std::ifstream fp1(filepath, std::ios::in|std::ios::binary);
    fp1.seekg(chunkNum*SEGMENT_SIZE, fp1.beg);

    write_log("sending data starting at " + to_string(fp1.tellg()));
    char buffer[SEGMENT_SIZE] = {0}; 
    int rc = 0;
    string sent = "";

    fp1.read(buffer, sizeof(buffer));
    int count = fp1.gcount();

    if ((rc = send(client_socket, buffer, count, 0)) == -1) {
        perror("[-]Error in sending file.");
        exit(1);
    }
    
    write_log("sent till "+to_string(fp1.tellg()));

    fp1.close();
} 

int write_chunk(int peersock, ll chunkNum, char* filepath){  
    
    int n, tot = 0;
    char buffer[SEGMENT_SIZE];

    string content = "";
    while (tot < SEGMENT_SIZE) {
        n = read(peersock, buffer, SEGMENT_SIZE-1);
        if (n <= 0){
            break;
        }
        buffer[n] = 0;
        fstream outfile(filepath, std::fstream::in | std::fstream::out | std::fstream::binary);
        outfile.seekp(chunkNum*SEGMENT_SIZE+tot, ios::beg);
        outfile.write(buffer, n);
        outfile.close();

        write_log("written at: "+ to_string(chunkNum*SEGMENT_SIZE + tot));
        write_log("written till: " + to_string(chunkNum*SEGMENT_SIZE + tot + n-1) +"\n");

        content += buffer;
        tot += n;
        bzero(buffer, SEGMENT_SIZE);
    }
    
    string hash = "";
    cal_string_hash(content, hash);
    hash.pop_back();
    hash.pop_back();
    if(hash != cur_file_PiecewiseHash[chunkNum]){
        is_corrupted_file = true;
    } 
    
    string filename = split_string(string(filepath), "/").back();
    update_file_chunk_bitvector(filename, chunkNum, chunkNum, false);

    return 0;
}

void get_bit_vector(peerFileDetails* pf){

    write_log("Getting chunk info of : "+ pf->filename + " from "+ pf->serverPeerIP);
    
    vector<string> serverPeerAddress = split_string(string(pf->serverPeerIP), ":");
    string command = "get_chunk_vector$$" + string(pf->filename);
    string response = connect_to_peer(&serverPeerAddress[0][0], &serverPeerAddress[1][0], command);

    for(size_t i=0; i<curdown_ch_ind_to_seederlist.size(); i++){
        if(response[i] == '1'){
            curdown_ch_ind_to_seederlist[i].push_back(string(pf->serverPeerIP));
        }
    }

    delete pf;
}

void getChunk(reqdChunkDetails* reqdChunk){

    write_log("Chunk fetching details :" + reqdChunk->filename + " " + 
            reqdChunk->serverPeerIP + " " + to_string(reqdChunk->chunkNum));

    string filename = reqdChunk->filename;
    vector<string> serverPeerIP = split_string(reqdChunk->serverPeerIP, ":");
    ll chunkNum = reqdChunk->chunkNum;
    string destination = reqdChunk->destination;

    string command = "get_chunk$$" + filename + "$$" + to_string(chunkNum) + "$$" + destination;
    connect_to_peer(&serverPeerIP[0][0], &serverPeerIP[1][0], command);
    
    delete reqdChunk;
    return;
}
 
void piece_selection_algorithm(vector<string> inpt, vector<string> peers){
    // inpt = [command, group id, filename, destination]
    ll filesize = stoll(peers.back());
    peers.pop_back();
    ll segments = filesize/SEGMENT_SIZE+1;
    curdown_ch_ind_to_seederlist.clear();
    curdown_ch_ind_to_seederlist.resize(segments);

    write_log("Started piecewise algo");
    
    vector<thread> threads, threads2;
 
    for(size_t i=0; i<peers.size(); i++){
        peerFileDetails* pf = new peerFileDetails();
        pf->filename = inpt[2];
        pf->serverPeerIP = peers[i];
        pf->filesize = segments;
        threads.push_back(thread(get_bit_vector, pf));
    }
    for(auto it=threads.begin(); it!=threads.end();it++){
        if(it->joinable()) it->join();
    }
    
    write_log("filled in default values to file");
    for(size_t i=0; i<curdown_ch_ind_to_seederlist.size(); i++){
        if(curdown_ch_ind_to_seederlist[i].size() == 0){
            cout << "All parts of the file are not available." << endl;
            return;
        }
    }

    threads.clear();
    srand((unsigned) time(0));
    ll segmentsReceived = 0;

    string des_path = inpt[3] + "/" + inpt[2];
    FILE* fp = fopen(&des_path[0], "r+");
	if(fp != 0){
		printf("The file already exists.\n") ;
        fclose(fp);
        return;
	}
    string ss(filesize, '\0');
    fstream in(&des_path[0],ios::out|ios::binary);
    in.write(ss.c_str(),strlen(ss.c_str()));  
    in.close();

    file_to_chunk_bitvector[inpt[2]].resize(segments,0);
    is_corrupted_file = false;

    vector<int> tmp(segments, 0);
    file_to_chunk_bitvector[inpt[2]] = tmp;
    
    string peerToGetFilepath;

    while(segmentsReceived < segments){
        write_log("getting segment no: " + to_string(segmentsReceived));
        
        ll randompiece;
        while(true){
            randompiece = rand()%segments;
            write_log("randompiece = " + to_string(randompiece));
            if(file_to_chunk_bitvector[inpt[2]][randompiece] == 0) break;
        }
        ll peersWithThisPiece = curdown_ch_ind_to_seederlist[randompiece].size();
        string randompeer = curdown_ch_ind_to_seederlist[randompiece][rand()%peersWithThisPiece];

        reqdChunkDetails* req = new reqdChunkDetails();
        req->filename = inpt[2];
        req->serverPeerIP = randompeer;
        req->chunkNum = randompiece;
        req->destination = inpt[3] + "/" + inpt[2];

        write_log("starting thread for chunk number "+ to_string(req->chunkNum));
        file_to_chunk_bitvector[inpt[2]][randompiece] = 1;

        threads2.push_back(thread(getChunk, req));
        segmentsReceived++;
        peerToGetFilepath = randompeer;
    }    
    for(auto it=threads2.begin(); it!=threads2.end();it++){
        if(it->joinable()) it->join();
    } 

    if(is_corrupted_file){
        cout << "Downloaded completed. The file may be corrupted." << endl;
    }
    else{
         cout << "Download completed. No corruption detected." << endl;
    }
    downloaded_files.insert({inpt[2], inpt[1]});

    vector<string> serverAddress = split_string(peerToGetFilepath, ":");
    connect_to_peer(&serverAddress[0][0], &serverAddress[1][0], "get_file_path$$" + inpt[2]);
    return;
}
 
int downloadFile(vector<string> inpt, int sock){
    // inpt -  download_file​ <group_id> <file_name> <destination_path>
    if(inpt.size() != 4){
        return 0;
    }
    string fileDetails = "";
    fileDetails += inpt[2] + "$$";
    fileDetails += inpt[3] + "$$";
    fileDetails += inpt[1];
    // fileDetails = [filename, destination, group id]
    
    write_log("sending file details for download : " + fileDetails);
    if(send(sock , &fileDetails[0] , strlen(&fileDetails[0]) , MSG_NOSIGNAL ) == -1){
        printf("Error: %s\n",strerror(errno));
        return -1;
    }

    char server_reply[524288] = {0}; 
    read(sock , server_reply, 524288); 

    if(string(server_reply) == "File not found"){
        cout << server_reply << endl;
        return 0;
    }
    vector<string> peersWithFile = split_string(server_reply, "$$");
    
    char dum[5];
    strcpy(dum, "test");
    write(sock, dum, 5);

    bzero(server_reply, 524288);
    read(sock , server_reply, 524288); 

    vector<string> tmp = split_string(string(server_reply), "$$");
    cur_file_PiecewiseHash = tmp;

    piece_selection_algorithm(inpt, peersWithFile);
    return 0;
}

int uploadFile(vector<string> inpt, int sock){
    if(inpt.size() != 3){
            return 0;
    }
    string fileDetails = "";
    char* filepath = &inpt[1][0];

    string filename = split_string(string(filepath), "/").back();

    if(is_uploaded[inpt[2]].find(filename) != is_uploaded[inpt[2]].end()){
        cout << "File already uploaded" << endl;
        if(send(sock , "error" , 5 , MSG_NOSIGNAL ) == -1){
            printf("Error: %s\n",strerror(errno));
            return -1;
        }
        return 0;
    }
    else{
        is_uploaded[inpt[2]][filename] = true;
        file_name_to_filePath[filename] = string(filepath);
    }

    string piecewiseHash = cal_hash(filepath);

    if(piecewiseHash == "$") return 0;
    string filehash = cal_file_hash(filepath);
    string filesize = to_string(file_size(filepath));

    fileDetails += string(filepath) + "$$";
    fileDetails += string(peer_ip) + ":" + to_string(peer_port) + "$$";
    fileDetails += filesize + "$$";
    fileDetails += filehash + "$$";
    fileDetails += piecewiseHash;
    
    write_log("sending file details for upload: " + fileDetails);
    if(send(sock , &fileDetails[0] , strlen(&fileDetails[0]) , MSG_NOSIGNAL ) == -1){
        printf("Error: %s\n",strerror(errno));
        return -1;
    }
    
    char server_reply[10240] = {0}; 
    read(sock , server_reply, 10240); 
    cout << server_reply << endl;
    write_log("server reply for send file: " + string(server_reply));

    update_file_chunk_bitvector(filename, 0, stoll(filesize)/SEGMENT_SIZE + 1, true);

    return 0;
}


// *************************************************************************************************************************

int main(int argc, char* argv[]){
    
    if(argc != 3){
        cout << "Give arguments as <peer IP:port> and <tracker info file name>\n";
        return -1;
    }
    configure_trackerinfo(argc, argv);

    int sock = 0; 
    struct sockaddr_in serv_addr; 
    pthread_t serverThread;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  
        printf("\n Socket creation error \n"); 
        return -1; 
    } 
    write_log("Peer socket created");

    if(pthread_create(&serverThread, NULL, run_as_server, NULL) == -1){
        perror("pthread"); 
        exit(EXIT_FAILURE); 
    }

    if(connect_to_tracker(1, serv_addr, sock) < 0){
        exit(-1); 
    }
    while(true){ 
        cout << ">> ";
        string inptline, s;
        getline(cin, inptline);

        if(inptline.length() < 1) continue;
        
        stringstream ss(inptline);
        vector<string> inpt;
        while(ss >> s){
            inpt.push_back(s);
        } 

        if(inpt[0] == "login" && is_loggedin){
            cout << "You already have one active session" << endl;
            continue;
        }
        if(inpt[0] != "login" && inpt[0] != "create_user" && !is_loggedin){
             cout << "Please login / create an account" << endl;
                continue;
        }

        if(send(sock , &inptline[0] , strlen(&inptline[0]) , MSG_NOSIGNAL ) == -1){
            printf("Error: %s\n",strerror(errno));
            return -1;
        }
        write_log("sent to server: " + inpt[0]);

        process_tracker_command(inpt, sock);
    }
    close(sock);
    return 0; 
}
