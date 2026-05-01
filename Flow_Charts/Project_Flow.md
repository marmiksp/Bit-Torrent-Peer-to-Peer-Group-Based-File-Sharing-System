# P2P File Sharing System - Detailed Flow Diagrams

This document provides comprehensive flow diagrams for the BitTorrent-style P2P file sharing system.

---

## Table of Contents

1. [System Architecture Overview](#1-system-architecture-overview)
2. [Component Interactions](#2-component-interactions)
3. [Client Initialization Flow](#3-client-initialization-flow)
4. [Tracker Server Flow](#4-tracker-server-flow)
5. [User Authentication Flow](#5-user-authentication-flow)
6. [Group Management Flow](#6-group-management-flow)
7. [File Upload Flow](#7-file-upload-flow)
8. [File Download Flow](#8-file-download-flow)
9. [Peer-to-Peer Communication](#9-peer-to-peer-communication)
10. [Piece Selection Algorithm](#10-piece-selection-algorithm)
11. [Data Structures](#11-data-structures)
12. [Message Protocol](#12-message-protocol)

---

## 1. System Architecture Overview

```
+------------------------------------------------------------------+
|                         NETWORK LAYER                             |
+------------------------------------------------------------------+
         |                    |                    |
         v                    v                    v
+------------------+  +------------------+  +------------------+
|   TRACKER        |  |   CLIENT 1       |  |   CLIENT 2       |
|   SERVER         |  |   (Peer)         |  |   (Peer)         |
+------------------+  +------------------+  +------------------+
| - User Auth      |  | - Client Thread  |  | - Client Thread  |
| - Group Mgmt     |  | - Server Thread  |  | - Server Thread  |
| - File Metadata  |  | - File Storage   |  | - File Storage   |
| - Seeder Lists   |  | - Chunk Mgmt     |  | - Chunk Mgmt     |
+------------------+  +------------------+  +------------------+
         ^                    ^                    ^
         |                    |                    |
         +--------------------+--------------------+
                              |
                    TCP Socket Connections
```

### Key Components

| Component | File | Responsibility |
|-----------|------|----------------|
| Tracker Server | `Tracker_Master.cpp` | Central coordinator for users, groups, file metadata |
| Client/Peer | `Client_Master.cpp` | Dual role: client (download) + server (upload chunks) |
| Common Utilities | `common.h` | Shared socket, string, file, and hash utilities |

---

## 2. Component Interactions

```
                    TRACKER SERVER
                          |
          +---------------+---------------+
          |                               |
          v                               v
    +-----------+                   +-----------+
    | Client A  |<----------------->| Client B  |
    +-----------+   P2P Transfer    +-----------+
          |                               |
          v                               v
    +-----------+                   +-----------+
    | Local     |                   | Local     |
    | Files     |                   | Files     |
    +-----------+                   +-----------+

Communication Types:
- Client <-> Tracker: Commands, metadata, seeder lists
- Client <-> Client:  Chunk bitvectors, chunk data transfer
```

---

## 3. Client Initialization Flow

```
                    +-------------------+
                    |   Start Client    |
                    | ./client args...  |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Parse Arguments   |
                    | my_ip, my_port    |
                    | tracker_ip, port  |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Initialize State  |
                    | - shared_files    |
                    | - file_chunks     |
                    | - file_hashes     |
                    +-------------------+
                            |
            +---------------+---------------+
            |                               |
            v                               v
    +-------------------+           +-------------------+
    | Start Peer Server |           | Connect to        |
    | (Background       |           | Tracker Server    |
    |  Thread)          |           |                   |
    +-------------------+           +-------------------+
            |                               |
            v                               v
    +-------------------+           +-------------------+
    | Listen on         |           | TCP Connect       |
    | my_ip:my_port     |           | tracker_ip:port   |
    +-------------------+           +-------------------+
            |                               |
            |                               v
            |                       +-------------------+
            |                       | Enter Command     |
            |                       | Loop (Main)       |
            +---------------------->+-------------------+
                                            |
                                            v
                                    +-------------------+
                                    | Read User Input   |
                                    | >> command args   |
                                    +-------------------+
                                            |
                                            v
                                    +-------------------+
                                    | Process Command   |
                                    +-------------------+
```

---

## 4. Tracker Server Flow

```
                    +-------------------+
                    |  Start Tracker    |
                    | ./tracker ip port |
                    +-------------------+
                            |
                            v
                    +-------------------+
                    | Create Socket     |
                    | Bind & Listen     |
                    +-------------------+
                            |
            +---------------+---------------+
            |                               |
            v                               v
    +-------------------+           +-------------------+
    | Quit Detection    |           | Accept            |
    | Thread            |           | Connections       |
    | (stdin monitor)   |           | (Main Loop)       |
    +-------------------+           +-------------------+
                                            |
                                            v
                                    +-------------------+
                                    | For Each Client   |
                                    | Spawn Thread      |
                                    +-------------------+
                                            |
                                            v
                                    +-------------------+
                                    | handle_client()   |
                                    +-------------------+
                                            |
                                            v
                    +---------------------------------------+
                    |         Command Processing Loop       |
                    +---------------------------------------+
                    |  recv_data() -> parse -> execute      |
                    |  -> send_data(response)               |
                    +---------------------------------------+
                            |
            +-------+-------+-------+-------+-------+
            |       |       |       |       |       |
            v       v       v       v       v       v
        create  login   create  join   upload  download
        _user           _group  _group  _file   _file
```

---

## 5. User Authentication Flow

```
CLIENT                                          TRACKER
  |                                                |
  |  create_user alice password123                 |
  |----------------------------------------------->|
  |                                                |
  |                          Check: user exists?   |
  |                          Store: users[alice]   |
  |                                                |
  |  OK$$Account created successfully              |
  |<-----------------------------------------------|
  |                                                |
  |  login alice password123                       |
  |----------------------------------------------->|
  |                                                |
  |                      Validate credentials      |
  |                      Check: already logged in? |
  |                      Set: user_logged_in=true  |
  |                                                |
  |  OK$$LOGIN_OK                                  |
  |<-----------------------------------------------|
  |                                                |
  |  192.168.1.101:6001  (my address)              |
  |----------------------------------------------->|
  |                                                |
  |                     Store: user_address[alice] |
  |                                                |
  |  OK$$Login successful                          |
  |<-----------------------------------------------|
  |                                                |


LOGIN STATE MACHINE:
+------------+      create_user     +------------+
|            |--------------------->|            |
|  No User   |                      |  Exists    |
|            |<---------------------|  (Not      |
+------------+      (error)         |  Logged In)|
                                    +------------+
                                          |
                                          | login (valid)
                                          v
                                    +------------+
                                    |  Logged In |
                                    +------------+
                                          |
                                          | logout / disconnect
                                          v
                                    +------------+
                                    |  Logged    |
                                    |  Out       |
                                    +------------+
```

---

## 6. Group Management Flow

```
GROUP LIFECYCLE:
                                    
    create_group                join_group              accept_request
         |                           |                        |
         v                           v                        v
+----------------+           +----------------+        +----------------+
|  Group Created |           | Request Added  |        | User Becomes   |
|  Admin = User  |           | to Pending     |        | Member         |
|  Members = [U] |           |                |        |                |
+----------------+           +----------------+        +----------------+


DETAILED FLOW:

    +-------------------+
    | create_group gid  |
    +-------------------+
            |
            v
    +-------------------+
    | Group exists?     |
    +-------------------+
       |           |
       No          Yes
       |           |
       v           v
+-------------+  +-------------+
| Create:     |  | ERROR:      |
| admin=user  |  | Group       |
| members=[u] |  | exists      |
+-------------+  +-------------+


    +-------------------+
    | join_group gid    |
    +-------------------+
            |
            v
    +-------------------+
    | Group exists?     |
    +-------------------+
       |           |
       Yes         No -> ERROR
       |
       v
    +-------------------+
    | Already member?   |
    +-------------------+
       |           |
       No          Yes -> ERROR
       |
       v
    +-------------------+
    | Add to pending    |
    | requests          |
    +-------------------+


    +-------------------+
    | accept_request    |
    | gid user_id       |
    +-------------------+
            |
            v
    +-------------------+
    | Is admin?         |
    +-------------------+
       |           |
       Yes         No -> ERROR
       |
       v
    +-------------------+
    | Has pending req?  |
    +-------------------+
       |           |
       Yes         No -> ERROR
       |
       v
    +-------------------+
    | Remove from       |
    | pending, add to   |
    | members           |
    +-------------------+
```

---

## 7. File Upload Flow

```
CLIENT                                          TRACKER
  |                                                |
  |  upload_file /path/to/file.mp4 movies          |
  |----------------------------------------------->|
  |                                                |
  |                    Validate: group exists      |
  |                    Validate: user is member    |
  |                                                |
  |  OK$$SEND_FILE_DETAILS                         |
  |<-----------------------------------------------|
  |                                                |
  |  [Client calculates hashes locally]            |
  |  - Read file in 512KB chunks                   |
  |  - SHA1 hash each chunk                        |
  |  - Get file size                               |
  |                                                |
  |  filename$$filesize$$filehash$$piece_hashes    |
  |----------------------------------------------->|
  |                                                |
  |                    Store metadata:             |
  |                    - file_seeders[gid][fname]  |
  |                    - file_sizes[fname]         |
  |                    - piece_hashes[fname]       |
  |                                                |
  |  OK$$File uploaded successfully                |
  |<-----------------------------------------------|
  |                                                |
  |  [Client updates local state]                  |
  |  - shared_files[fname] = path                  |
  |  - file_chunks[fname] = [1,1,1,...]            |
  |                                                |


HASH CALCULATION:

+------------------+
|     FILE         |
| (e.g., 1.5 MB)   |
+------------------+
         |
         v
+--------+--------+--------+
| Chunk0 | Chunk1 | Chunk2 |
| 512KB  | 512KB  | 512KB  |
+--------+--------+--------+
    |        |        |
    v        v        v
 SHA1()   SHA1()   SHA1()
    |        |        |
    v        v        v
  hash0    hash1    hash2
    |        |        |
    +--------+--------+
             |
             v
    "hash0,hash1,hash2"
    (sent to tracker)
```

---

## 8. File Download Flow

```
CLIENT (Downloader)              TRACKER                    PEERS (Seeders)
       |                            |                            |
       |  download_file gid fname   |                            |
       |      dest_path             |                            |
       |--------------------------->|                            |
       |                            |                            |
       |              Validate membership                        |
       |              Find active seeders                        |
       |              Get file metadata                          |
       |                            |                            |
       |  OK$$SEEDER_LIST$$         |                            |
       |  peer1,peer2$$size$$hashes |                            |
       |<---------------------------|                            |
       |                            |                            |
       |                                                         |
       |  GET_BITVECTOR$$filename                                |
       |-------------------------------------------------------->|
       |                                                         |
       |                                    [For each peer]      |
       |                                    Return: "11101..."   |
       |                                                         |
       |  OK$$11101...  (bitvector)                              |
       |<--------------------------------------------------------|
       |                                                         |
       |  [Build chunk availability map]                         |
       |  chunk0 -> [peer1, peer2]                               |
       |  chunk1 -> [peer1]                                      |
       |  chunk2 -> [peer2]                                      |
       |  ...                                                    |
       |                                                         |
       |  [Random piece selection loop]                          |
       |                                                         |
       |  GET_CHUNK$$filename$$chunk_num                         |
       |-------------------------------------------------------->|
       |                                                         |
       |                                    Read chunk from file |
       |                                                         |
       |  OK$$chunk_size                                         |
       |<--------------------------------------------------------|
       |                                                         |
       |  READY                                                  |
       |-------------------------------------------------------->|
       |                                                         |
       |  [raw chunk bytes...]                                   |
       |<--------------------------------------------------------|
       |                                                         |
       |  [Verify SHA1 hash]                                     |
       |  [Write to destination file]                            |
       |  [Update bitvector]                                     |
       |  [Repeat for all chunks]                                |
       |                                                         |


DOWNLOAD STATE:

+----------------+
| Start Download |
+----------------+
        |
        v
+----------------+
| Get Seeder     |
| List from      |
| Tracker        |
+----------------+
        |
        v
+----------------+
| Query All      |
| Peers for      |
| Bitvectors     |
+----------------+
        |
        v
+----------------+
| All Chunks     |----No---> ERROR: Incomplete
| Available?     |
+----------------+
        |
       Yes
        v
+----------------+
| Create Empty   |
| Dest File      |
+----------------+
        |
        v
+----------------+<---------+
| Select Random  |          |
| Missing Chunk  |          |
+----------------+          |
        |                   |
        v                   |
+----------------+          |
| Select Random  |          |
| Peer with      |          |
| Chunk          |          |
+----------------+          |
        |                   |
        v                   |
+----------------+          |
| Download Chunk |          |
| Verify Hash    |          |
| Write to File  |          |
+----------------+          |
        |                   |
        v                   |
+----------------+          |
| More Chunks?   |---Yes----+
+----------------+
        |
       No
        v
+----------------+
| Download       |
| Complete!      |
| Become Seeder  |
+----------------+
```

---

## 9. Peer-to-Peer Communication

```
PEER SERVER (run_peer_server):

+-------------------+
| Create Socket     |
| Bind my_ip:port   |
+-------------------+
        |
        v
+-------------------+
| Listen            |
+-------------------+
        |
        v
+-------------------+<-------+
| Accept Connection |        |
+-------------------+        |
        |                    |
        v                    |
+-------------------+        |
| Spawn Thread      |--------+
| handle_peer_req() |
+-------------------+


PEER REQUEST HANDLING (handle_peer_request):

+-------------------+
| recv_data()       |
| Parse command     |
+-------------------+
        |
        +--------+--------+
        |        |        |
        v        v        v
    GET_BIT   GET_     Unknown
    VECTOR    CHUNK
        |        |        |
        v        v        v
+----------+ +----------+ +----------+
| Return   | | Read     | | ERROR    |
| bitvec   | | chunk    | |          |
| string   | | from     | |          |
| "11101"  | | file     | |          |
+----------+ +----------+ +----------+
                  |
                  v
             +----------+
             | Send OK  |
             | + size   |
             +----------+
                  |
                  v
             +----------+
             | Wait for |
             | READY    |
             +----------+
                  |
                  v
             +----------+
             | Send raw |
             | chunk    |
             | bytes    |
             +----------+
```

---

## 10. Piece Selection Algorithm

```
ALGORITHM: Random Piece Selection

Input:
- List of seeders (peer addresses)
- Filename
- File size
- Piece hash list

Step 1: Query all peers for bitvectors
+-------------------------------------------+
| For each peer in seeders:                 |
|   bitvector = get_peer_bitvector(peer)    |
|   For each bit i in bitvector:            |
|     if bit[i] == 1:                       |
|       chunk_availability[i].add(peer)     |
+-------------------------------------------+

Step 2: Verify availability
+-------------------------------------------+
| For i = 0 to num_chunks:                  |
|   if chunk_availability[i] is empty:      |
|     return ERROR "chunk unavailable"      |
+-------------------------------------------+

Step 3: Download loop
+-------------------------------------------+
| while chunks_downloaded < num_chunks:     |
|                                           |
|   # Random piece selection                |
|   missing = [i for i if !have_chunk[i]]   |
|   chunk_idx = random_choice(missing)      |
|                                           |
|   # Random peer selection                 |
|   peers = chunk_availability[chunk_idx]   |
|   peer = random_choice(peers)             |
|                                           |
|   # Download and verify                   |
|   success = download_chunk(peer, idx)     |
|                                           |
|   if success:                             |
|     have_chunk[idx] = true                |
|     chunks_downloaded++                   |
|   else:                                   |
|     # Remove failed peer                  |
|     peers.remove(peer)                    |
|     if peers.empty():                     |
|       return ERROR                        |
+-------------------------------------------+


VISUALIZATION:

Chunks:     [0] [1] [2] [3] [4]
Peer A:      1   1   0   1   1
Peer B:      1   0   1   1   0
Peer C:      0   1   1   0   1
            ─────────────────────
Available:  A,B  A,C B,C A   A,C

Download Order (random): 2, 4, 0, 3, 1

Step 1: Download chunk[2] from random(B,C) = C
Step 2: Download chunk[4] from random(A,C) = A
Step 3: Download chunk[0] from random(A,B) = B
Step 4: Download chunk[3] from A
Step 5: Download chunk[1] from random(A,C) = C
```

---

## 11. Data Structures

```
TRACKER DATA STRUCTURES:

+------------------------+---------------------------+
| Variable               | Type                      |
+------------------------+---------------------------+
| users                  | map<username, password>   |
| user_logged_in         | map<username, bool>       |
| user_address           | map<username, "ip:port">  |
| group_admin            | map<group_id, username>   |
| group_members          | map<group_id, set<user>>  |
| group_pending          | map<group_id, set<user>>  |
| file_seeders           | map<gid, map<fname,       |
|                        |     set<username>>>       |
| file_sizes             | map<filename, size_str>   |
| piece_hashes           | map<filename, hash_str>   |
+------------------------+---------------------------+


CLIENT DATA STRUCTURES:

+------------------------+---------------------------+
| Variable               | Type                      |
+------------------------+---------------------------+
| shared_files           | map<filename, filepath>   |
| file_chunks            | map<filename, vector<bool>|
| file_hashes            | map<filename, vector<str>>|
| downloaded_files       | map<filename, group_id>   |
| is_logged_in           | atomic<bool>              |
| current_user           | string                    |
+------------------------+---------------------------+


RELATIONSHIP DIAGRAM:

    users
      |
      +---> user_logged_in
      |
      +---> user_address -----> [Peer Server]
      |
      +---> group_members <---- group_admin
                  |
                  v
            file_seeders
                  |
                  +---> file_sizes
                  |
                  +---> piece_hashes
```

---

## 12. Message Protocol

```
MESSAGE FORMAT:

Request:  <command> <arg1> <arg2> ...
Response: <status>$$<message>

Status codes:
- OK       : Success
- ERROR:   : Failure

Delimiter: "$$"


COMMAND REFERENCE:

+------------------+--------------------------------+--------------------------------+
| Command          | Request Format                 | Response Format                |
+------------------+--------------------------------+--------------------------------+
| create_user      | create_user user pass          | OK$$Account created            |
| login            | login user pass                | OK$$LOGIN_OK (then send addr)  |
| logout           | logout                         | OK$$Logged out                 |
| create_group     | create_group gid               | OK$$Group created              |
| join_group       | join_group gid                 | OK$$Join request sent          |
| list_groups      | list_groups                    | OK$$grp1,grp2,grp3             |
| list_requests    | list_requests gid              | OK$$user1,user2                |
| accept_request   | accept_request gid uid         | OK$$Request accepted           |
| list_files       | list_files gid                 | OK$$file1,file2,file3          |
| upload_file      | upload_file path gid           | OK$$SEND_FILE_DETAILS          |
| download_file    | download_file gid fname dest   | OK$$SEEDER_LIST$$...           |
| stop_share       | stop_share gid fname           | OK$$Stopped sharing            |
+------------------+--------------------------------+--------------------------------+


PEER-TO-PEER PROTOCOL:

+------------------+--------------------------------+--------------------------------+
| Command          | Request Format                 | Response Format                |
+------------------+--------------------------------+--------------------------------+
| GET_BITVECTOR    | GET_BITVECTOR$$filename        | OK$$11101010                   |
| GET_CHUNK        | GET_CHUNK$$filename$$chunk_num | OK$$chunk_size, then raw data  |
+------------------+--------------------------------+--------------------------------+


UPLOAD FLOW MESSAGES:

Client -> Tracker: "upload_file /path/file.mp4 movies"
Tracker -> Client: "OK$$SEND_FILE_DETAILS"
Client -> Tracker: "filename$$size$$hash$$piece_hashes"
Tracker -> Client: "OK$$File uploaded successfully"


DOWNLOAD FLOW MESSAGES:

Client -> Tracker: "download_file movies file.mp4 /dest"
Tracker -> Client: "OK$$SEEDER_LIST$$ip1:port1,ip2:port2$$filesize$$hash1,hash2,hash3"

Client -> Peer:    "GET_BITVECTOR$$file.mp4"
Peer -> Client:    "OK$$11101"

Client -> Peer:    "GET_CHUNK$$file.mp4$$2"
Peer -> Client:    "OK$$524288"
Client -> Peer:    "READY"
Peer -> Client:    [512KB raw bytes]
```

---

## Summary

This P2P file sharing system implements a BitTorrent-like architecture with:

1. **Centralized Tracker**: Manages users, groups, and file metadata
2. **Decentralized File Transfer**: Chunks downloaded directly from peers
3. **Multi-threaded Design**: Concurrent client handling and peer serving
4. **Integrity Verification**: SHA1 hash per chunk ensures data integrity
5. **Random Piece Selection**: Load balancing across multiple seeders

The design allows for network deployment across multiple machines while maintaining clean separation between tracker coordination and peer-to-peer data transfer.
