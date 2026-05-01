# P2P File Sharing System — Complete Reference

This document is the single source of truth for understanding how the BitTorrent-style
P2P file sharing system works — from the big picture down to individual bytes on the wire.

---

## Table of Contents

1.  [System Overview — Textual Explanation](#1-system-overview--textual-explanation)
2.  [Architecture Diagram](#2-architecture-diagram)
3.  [Component Responsibilities](#3-component-responsibilities)
4.  [How the TCP Message Protocol Works](#4-how-the-tcp-message-protocol-works)
5.  [Client Initialization Flow](#5-client-initialization-flow)
6.  [Tracker Server Flow](#6-tracker-server-flow)
7.  [User Authentication Flow](#7-user-authentication-flow)
8.  [Group Management Flow](#8-group-management-flow)
9.  [File Upload — Complete Detailed Flow](#9-file-upload--complete-detailed-flow)
10. [File Download — Complete Detailed Flow](#10-file-download--complete-detailed-flow)
11. [Parallel Download Deep Dive](#11-parallel-download-deep-dive)
12. [Peer-to-Peer Chunk Serving](#12-peer-to-peer-chunk-serving)
13. [Data Structures Reference](#13-data-structures-reference)
14. [Full Command & Protocol Reference](#14-full-command--protocol-reference)

---

## 1. System Overview — Textual Explanation

### What This System Is

This is a **group-based peer-to-peer file sharing system** modelled after BitTorrent. Instead
of downloading a file from one central server, files are split into small pieces (chunks) and
downloaded simultaneously from multiple peers who already have those pieces. Anyone who
finishes downloading automatically becomes a source (seeder) for others.

The "group-based" part adds an access-control layer that vanilla BitTorrent does not have:
files are shared within named groups, and only group members can see or download those files.

---

### The Three Roles in the System

**1. The Tracker Server (`Tracker_Master.cpp`)**

The tracker is the central directory of the system. It does NOT store any file data — it only
stores metadata: who the users are, which groups exist, and for each group, which files have
been shared and who currently has them. Think of it like a library catalogue — it tells you
which shelf a book is on and who has borrowed it, but it does not contain the books itself.

When a client wants to download a file, it asks the tracker "who has this file?" The tracker
responds with a list of peer addresses (IP:port). After that, the tracker is not involved in
the actual file transfer at all.

**2. The Client/Peer (`Client_Master.cpp`)**

Every participant runs the client program. The client plays two roles simultaneously:

- **As a client:** It connects to the tracker to register, manage groups, and get seeder
  lists. It also connects to other peers to download file chunks.
- **As a server:** It runs a background thread (`run_peer_server`) that listens on a port
  and accepts incoming connections from other peers who want chunks from it.

This dual role is what makes the system peer-to-peer: every downloader is simultaneously a
potential uploader.

**3. Common Utilities (`common.h`)**

A shared header included by both programs. It provides: TCP socket helpers, the
length-prefix message framing protocol, SHA1/SHA256 hash functions, file utilities, and a
thread-safe logger. Because both programs include the same header, they speak exactly the
same wire protocol.

---

### The Big Picture — What Happens When You Download a File

Suppose Alice uploads `movie.mp4` to the group `cs_students`, and Bob wants to download it.

```
Step 1  Bob asks the tracker: "Who has movie.mp4 in cs_students?"
Step 2  Tracker responds with: "Alice has it. Her address is 192.168.1.5:6001"
Step 3  Bob connects to Alice's peer server and asks: "Which chunks do you have?"
Step 4  Alice responds with a bitvector: "11111111" (she has all 8 chunks)
Step 5  Bob's workers start downloading chunks in parallel from Alice
        (If there were multiple seeders, Bob would download from all of them at once)
Step 6  Each chunk is verified with SHA1 before being written to disk
Step 7  Once ALL chunks are downloaded and the whole-file SHA256 hash is verified,
        Bob sends "register_seeder" to the tracker
Step 8  Now Bob is listed as a seeder too — Charlie can download from both Alice and Bob
```

---

### Why Chunks?

A 1 GB file is split into chunks of 512 KB each (about 2048 chunks). This enables:

- **Parallel downloading:** Different chunks come from different peers at the same time.
- **Integrity checking:** Each chunk has its own SHA1 hash. If a chunk is corrupted in
  transit, only that 512 KB needs to be re-downloaded, not the whole file.
- **Incremental sharing:** As soon as Bob has chunk 0, he can serve chunk 0 to Charlie
  even while Bob is still downloading chunks 1–2047.

---

### Why SHA1 Per Chunk AND SHA256 for the Whole File?

**SHA1 per chunk** — verified immediately after receiving each 512 KB piece. Catches
corruption or a malicious peer sending bad data. If the hash fails, the chunk is discarded
and re-requested from a different peer.

**SHA256 of the file** — this is computed as `sha256(all_chunk_hashes_joined_with_comma)`.
It acts as a fingerprint for the complete file. When the tracker gives a downloader the
piece hash list, it also gives this master hash. After all chunks are written to disk, the
downloader recomputes this hash from the received piece hashes and compares — if they match,
the piece hash list was not tampered with and the file is genuine.

This forms a chain of trust:
```
sha256(piecewise_hash_string) == file_hash   ← verifies the hash list was not tampered
sha1(chunk_data) == piece_hash[i]            ← verifies each chunk was not corrupted
```

---

### Why Register as a Seeder Only AFTER Download Completes?

In an earlier design, the tracker added the downloader to the seeder list the moment they
requested the file — before any data was transferred. This caused a bug: if Bob's download
failed halfway, he would remain listed as a seeder with zero chunks. Other clients would
connect to Bob, ask for chunks, get errors, and waste time.

The fix: Bob only sends `register_seeder` to the tracker after every chunk has passed its
SHA1 check AND the whole-file SHA256 hash matches. The tracker's seeder list always reflects
peers who actually have the complete file.

---

## 2. Architecture Diagram

```
+------------------------------------------------------------------+
|                         NETWORK LAYER (TCP)                      |
+------------------------------------------------------------------+
         |                    |                    |
         v                    v                    v
+------------------+  +------------------+  +------------------+
|   TRACKER        |  |   CLIENT A       |  |   CLIENT B       |
|   SERVER         |  |   (Alice)        |  |   (Bob)          |
+------------------+  +------------------+  +------------------+
| - User Auth      |  | - Main Thread    |  | - Main Thread    |
| - Group Mgmt     |  |   (commands +    |  |   (commands +    |
| - File Metadata  |  |    downloads)    |  |    downloads)    |
| - Seeder Lists   |  | - Peer Server    |  | - Peer Server    |
| - Hash Registry  |  |   Thread         |  |   Thread         |
+------------------+  | - Worker Threads |  | - Worker Threads |
         ^            |   (parallel      |  |   (parallel      |
         |            |    chunk DL)     |  |    chunk DL)     |
         |            +------------------+  +------------------+
         |                    ^                    ^
         |                    |                    |
         +--------------------+--------------------+
                    Tracker connections (persistent TCP)

                    Alice <---> Bob (direct P2P chunk transfer)
```

---

## 3. Component Responsibilities

| Component          | File                  | Key Responsibilities                                           |
|--------------------|-----------------------|----------------------------------------------------------------|
| Tracker Server     | `Tracker_Master.cpp`  | User registry, group CRUD, file metadata, seeder lists         |
| Client / Peer      | `Client_Master.cpp`   | Dual role: user-facing client + background peer upload server  |
| Common Utilities   | `common.h`            | Framed TCP send/recv, SHA1/SHA256, file utilities, logger      |

### Thread model

**Tracker:** one thread per connected client (spawned via `thread::detach`), plus a stdin
monitor thread for the `quit` command.

**Client:** three kinds of threads:
- **Main thread** — reads user commands, talks to tracker, orchestrates downloads
- **Peer server thread** — `run_peer_server()`, accepts incoming peer connections, spawns a
  handler thread per connection
- **Download worker threads** — up to `MAX_PARALLEL_DOWNLOADS` (4) threads per active
  download, each independently fetching chunks from peers

---

## 4. How the TCP Message Protocol Works

### The Problem with Raw TCP

TCP is a **stream protocol** — data flows as a continuous byte stream with no built-in message
boundaries. If sender sends "HELLO" then "WORLD", the receiver might get "HELLOWORLD" in one
`recv()` call, or "HEL" then "LOWORLD" in two calls. There is no guarantee a single `recv()`
returns exactly one logical message.

The old code used a single `recv()` call with a 64 KB limit and treated whatever arrived as
a complete message. This caused two bugs:
1. A message larger than 64 KB was silently truncated (a seeder list with many peers, or a
   large piece hash list).
2. A message arriving in two TCP segments was silently incomplete.

### The Fix: Length-Prefix Framing

Every message is now preceded by a 4-byte header containing the message body length in
**network byte order** (big-endian). The receiver reads exactly 4 bytes first, converts to a
local integer (`ntohl`), then reads exactly that many bytes for the body.

```
Wire format for every string message:
+----------+-----------------------------+
|  4 bytes |   N bytes                  |
|  length  |   message body             |
|  (uint32)|   (UTF-8 text)             |
+----------+-----------------------------+
  htonl(N)    "OK$$Login successful"
```

**Sender (`send_data`):**
```
1. Compute N = message.size()
2. Convert to network byte order: net_len = htonl(N)
3. Send the 4 bytes of net_len (loop until all 4 sent)
4. Send the N bytes of the message body (loop until all sent)
```

**Receiver (`recv_data`):**
```
1. recv() exactly 4 bytes into a uint32_t
2. Convert to host order: body_len = ntohl(net_len)
3. Call recv_exact(socket, body_len) — loops until exactly body_len bytes received
4. Return the body string
```

**Raw chunk data is NOT framed.** The binary chunk transfer uses bare `send()`/`recv()` calls
because its length is negotiated by the preceding framed `"OK$$<size>"` handshake message.
Framing on top of framing would double-count the length.

### Message Delimiter

Within the text body, fields are separated by `"$$"`:
```
"OK$$Login successful"
"ERROR:$$User already exists"
"OK$$SEEDER_LIST$$192.168.1.5:6001,192.168.1.6:6002$$10485760$$hash0,hash1,hash2$$abc123ef"
```

---

## 5. Client Initialization Flow

```
./client 192.168.1.101 6000 192.168.1.100 5000
           |           |    |             |
        my_ip       my_port tracker_ip  tracker_port
```

```
main()
  |
  +-- Parse and store: my_ip, my_port, tracker_ip, tracker_port
  |
  +-- Start peer server thread (run_peer_server)
  |     |
  |     +-- create_socket() + bind_and_listen(my_ip, my_port)
  |     +-- Loop: accept() -> spawn handle_peer_request thread (detached)
  |
  +-- create_socket() for tracker connection
  +-- connect_to_server(tracker_ip, tracker_port)
  |
  +-- print_help()
  |
  +-- Main command loop:
        while (true):
          print ">> "
          getline(cin, line)
          parse line into args[]
          dispatch to appropriate handler
```

---

## 6. Tracker Server Flow

```
./tracker 192.168.1.100 5000
```

```
main()
  |
  +-- create_socket() + bind_and_listen(tracker_ip, tracker_port)
  |
  +-- Start quit detection thread
  |     (reads stdin; exits process on "quit")
  |
  +-- Main accept loop:
        while (true):
          client_socket = accept()
          thread(handle_client, client_socket).detach()


handle_client(client_socket):
  |
  +-- Loop:
        message = recv_data(client_socket)      <- framed recv
        parse into args[]
        dispatch command -> build response
        send_data(client_socket, response)      <- framed send
  |
  +-- On disconnect: set user_logged_in[username] = false
  +-- close(client_socket)
```

Commands handled by `handle_client`:
`create_user`, `login`, `logout`, `create_group`, `join_group`, `leave_group`,
`list_groups`, `list_requests`, `accept_request`, `change_admin`, `list_files`,
`upload_file`, `download_file`, `register_seeder`, `stop_share`

---

## 7. User Authentication Flow

```
CLIENT                                              TRACKER
  |                                                    |
  |  [framed] "create_user alice secret123"            |
  |------------------------------------------------>   |
  |                                                    |
  |                               users["alice"] = "secret123"
  |                               user_logged_in["alice"] = false
  |                                                    |
  |  [framed] "OK$$Account created successfully"       |
  |<------------------------------------------------   |
  |                                                    |
  |  [framed] "login alice secret123"                  |
  |------------------------------------------------>   |
  |                                                    |
  |                               validate credentials
  |                               check not already logged in
  |                               user_logged_in["alice"] = true
  |                               client_username = "alice"
  |                                                    |
  |  [framed] "OK$$LOGIN_OK"                           |
  |<------------------------------------------------   |
  |                                                    |
  |  [framed] "192.168.1.101:6001"  <- my peer port   |
  |------------------------------------------------>   |
  |                                                    |
  |                               user_address["alice"] = "192.168.1.101:6001"
  |                                                    |
  |  [framed] "OK$$Login successful"                   |
  |<------------------------------------------------   |
  |                                                    |
  |  is_logged_in = true, current_user = "alice"       |
```

**State machine:**

```
[No Account] --create_user--> [Account Exists, Logged Out]
                                      |
                               login (valid creds)
                                      |
                                      v
                              [Logged In] --logout/disconnect--> [Logged Out]
```

**Important detail:** The client sends its peer-server address (`my_ip:my_port`) as a
separate message immediately after receiving `LOGIN_OK`. The tracker uses `getpeername()` to
trust the actual IP of the TCP connection, and only trusts the self-reported PORT number,
since the listening peer server port differs from the ephemeral source port.

---

## 8. Group Management Flow

### Create Group
```
Client: "create_group cs_students"
Tracker checks: group_admin["cs_students"] does not exist
Tracker sets:   group_admin["cs_students"] = "alice"
                group_members["cs_students"] = {"alice"}
Response: "OK$$Group created successfully"
```

### Join Group (two-step: request + accept)
```
Bob: "join_group cs_students"
Tracker: group_pending["cs_students"].insert("bob")
Response: "OK$$Join request sent"

Alice (admin): "list_requests cs_students"
Response: "OK$$bob"

Alice: "accept_request cs_students bob"
Tracker: group_pending["cs_students"].erase("bob")
         group_members["cs_students"].insert("bob")
Response: "OK$$Request accepted"
```

### Leave Group
- Regular member: removed from `group_members`, removed from all seeder lists in that group
- Admin: must first transfer admin via `change_admin <group_id> <new_admin>`, then leave
- Admin cannot leave if they are the sole member (group must be deleted)

---

## 9. File Upload — Complete Detailed Flow

This section covers every step from the user typing the command to the tracker having all
the metadata stored.

### Step 1 — Client-side validation

```
User types: upload_file /home/alice/movie.mp4 cs_students
```

Client checks locally:
```
path_exists("/home/alice/movie.mp4")  -> must be true
get_file_size("/home/alice/movie.mp4") -> returns filesize in bytes
get_filename("/home/alice/movie.mp4")  -> "movie.mp4"
```
If the file does not exist, the error is printed locally without contacting the tracker.

### Step 2 — Initial tracker handshake

```
CLIENT                                                  TRACKER
  |                                                        |
  |  [framed] "upload_file /home/alice/movie.mp4 cs_students"
  |------------------------------------------------------> |
  |                                                        |
  |                          validate: group "cs_students" exists?
  |                          validate: "alice" is a member?
  |                                                        |
  |  [framed] "OK$$SEND_FILE_DETAILS"                      |
  |<------------------------------------------------------ |
```

### Step 3 — Hash computation (client side, local)

The client now computes all hashes locally without re-contacting the tracker:

```
calculate_piece_hashes("/home/alice/movie.mp4"):
  Open file in binary mode
  Read 512 KB into buffer
  sha1_hash(buffer) -> "a3f9c2..."   <- piece_hash[0]
  Read next 512 KB
  sha1_hash(buffer) -> "b7e1d4..."   <- piece_hash[1]
  ... repeat until EOF ...
  Returns: ["a3f9c2...", "b7e1d4...", "c9f3a1...", ...]

piecewise_hash = "a3f9c2...,b7e1d4...,c9f3a1..."   (joined with commas)
filehash       = sha256_hash(piecewise_hash)         (fingerprint of the whole file)
filesize       = 10485760   (example: 10 MB)
```

**Why sha256(piecewise_hash) as the file hash?**
Because it creates a chain: if any single chunk's SHA1 changes, the piecewise_hash string
changes, and therefore the SHA256 of it changes. A single hash captures the integrity of the
entire file without reading the file a second time.

### Step 4 — Send file details to tracker

```
CLIENT                                                  TRACKER
  |                                                        |
  |  [framed] "movie.mp4$$10485760$$<filehash>$$<piece_hashes>"
  |------------------------------------------------------> |
  |                                                        |
  |                          parse: filename, filesize, filehash, piecewise_hash
  |                          store under lock:
  |                            file_seeders["cs_students"]["movie.mp4"].insert("alice")
  |                            file_sizes["movie.mp4"]   = "10485760"
  |                            piece_hashes["movie.mp4"] = "a3f9c2...,b7e1d4..."
  |                            file_hashes["movie.mp4"]  = "<sha256>"
  |                                                        |
  |  [framed] "OK$$File uploaded successfully"             |
  |<------------------------------------------------------ |
```

### Step 5 — Client local state update

```
lock file_mutex
  shared_files["movie.mp4"]  = "/home/alice/movie.mp4"  <- peer server can now serve it
  file_chunks["movie.mp4"]   = [true, true, true, ...]   <- all chunks owned
  file_hashes["movie.mp4"]   = ["a3f9c2...", "b7e1d4..."]
unlock
```

Alice's peer server is now ready to serve any chunk of `movie.mp4` to other peers.

### Upload Flow Summary Diagram

```
User Input
    |
    v
[Validate file exists locally]
    |
    v
[Send "upload_file path group" to tracker]
    |
    v
[Tracker validates membership] --> ERROR if not member
    |
    v
[Tracker sends "OK$$SEND_FILE_DETAILS"]
    |
    v
[Client reads file in 512KB chunks]
[SHA1 hash each chunk -> piece_hash_list]
[SHA256(joined_piece_hashes) -> filehash]
    |
    v
[Send "filename$$size$$filehash$$piece_hashes" to tracker]
    |
    v
[Tracker stores all metadata under data_mutex]
[file_seeders, file_sizes, piece_hashes, file_hashes all updated]
    |
    v
[Tracker sends "OK$$File uploaded successfully"]
    |
    v
[Client updates shared_files, file_chunks, file_hashes]
[Peer server now ready to serve this file]
    |
    v
DONE
```

---

## 10. File Download — Complete Detailed Flow

This section covers every step from the user command to the file being saved and the client
registered as a seeder. This is the most complex flow in the system.

### Step 1 — Client validation

```
User types: download_file cs_students movie.mp4 /home/bob/downloads
```

Client checks locally:
```
is_directory("/home/bob/downloads")                    -> must be true
path_exists("/home/bob/downloads/movie.mp4")           -> must be false (no overwrite)
full_dest_path = "/home/bob/downloads/movie.mp4"
```

### Step 2 — Get seeder list from tracker

```
CLIENT                                                  TRACKER
  |                                                        |
  |  [framed] "download_file cs_students movie.mp4 /home/bob/downloads"
  |------------------------------------------------------> |
  |                                                        |
  |                          validate: group exists?
  |                          validate: bob is a member?
  |                          validate: movie.mp4 exists in group?
  |                          build active_seeders list:
  |                            for each seeder of movie.mp4:
  |                              if user_logged_in[seeder] AND user_address known:
  |                                add user_address[seeder] to list
  |                          (Bob is NOT added as seeder yet)
  |                                                        |
  |  [framed] "OK$$SEEDER_LIST$$192.168.1.5:6001$$10485760$$a3f9...,b7e1...$$<filehash>"
  |<------------------------------------------------------ |
```

**Response format parsed by client:**
```
parts[0] = "OK"
parts[1] = "SEEDER_LIST"
parts[2] = "192.168.1.5:6001,192.168.1.6:6002"    <- comma-separated peer addresses
parts[3] = "10485760"                               <- filesize in bytes
parts[4] = "a3f9c2...,b7e1d4...,c9f3a1..."         <- comma-separated piece hashes
parts[5] = "<sha256_of_all_piece_hashes>"           <- whole-file hash
```

```cpp
seeders          = ["192.168.1.5:6001", "192.168.1.6:6002"]
filesize         = 10485760
piece_hash_list  = ["a3f9c2...", "b7e1d4...", "c9f3a1..."]
expected_filehash = "<sha256>"
num_chunks       = ceil(10485760 / 524288) = 20
```

### Step 3 — Parallel bitvector collection (Phase 1 of download)

Bob fires one background thread per seeder simultaneously:

```
std::async(get_peer_bitvector, "192.168.1.5:6001", "movie.mp4")  -> Thread A
std::async(get_peer_bitvector, "192.168.1.6:6002", "movie.mp4")  -> Thread B
```

Each thread:
```
CLIENT THREAD A                           PEER (Alice - 192.168.1.5:6001)
  |                                               |
  |  create TCP socket                            |
  |  connect to 192.168.1.5:6001                 |
  |  [framed] "GET_BITVECTOR$$movie.mp4"          |
  |---------------------------------------------> |
  |                                               |
  |                      lock file_mutex
  |                      read file_chunks["movie.mp4"] = [T,T,T,...,T]
  |                      build bitvector: "11111111111111111111"
  |                      unlock
  |                                               |
  |  [framed] "OK$$11111111111111111111"          |
  |<--------------------------------------------- |
  |  close socket                                 |
```

Bob collects both bitvectors and builds `chunk_availability`:
```
chunk_availability[0]  = ["192.168.1.5:6001", "192.168.1.6:6002"]
chunk_availability[1]  = ["192.168.1.5:6001"]
chunk_availability[2]  = ["192.168.1.6:6002"]
... etc
```

If any chunk index is missing from `chunk_availability` (no peer has it), download aborts
with "Chunk X not available from any peer."

### Step 4 — Pre-allocate destination file

```cpp
ofstream file("/home/bob/downloads/movie.mp4", ios::binary);
if (filesize > 0) {
    file.seekp(filesize - 1);    // seek to last byte
    file.write("\0", 1);         // write one null byte to expand file to full size
}
file.close();
// File on disk is now 10 MB of zeros — a placeholder
```

This pre-allocation means workers can immediately `pwrite()` any chunk at any offset without
the file growing incrementally, avoiding filesystem fragmentation.

### Step 5 — Open raw file descriptor for parallel writes

```cpp
int fd = open("/home/bob/downloads/movie.mp4", O_RDWR);
```

A POSIX file descriptor (integer) is passed to all worker threads. Unlike C++ `fstream`,
`pwrite(fd, buf, size, offset)` does NOT use a shared cursor — it takes the offset as a
parameter, making simultaneous writes to different offsets from different threads safe.

### Step 6 — Parallel chunk download (Phase 2 of download)

Bob builds a `DownloadState` and spawns worker threads:

```
DownloadState:
  pending = queue{0, 1, 2, 3, 4, 5, ..., 19}    <- all 20 chunks
  chunk_availability = { 0:[Alice,Charlie], 1:[Alice], ... }
  chunks_done = 0
  fatal_error = false
  num_chunks  = 20

num_workers = min(2 seeders, 4 MAX, 20 chunks) = 2

Thread W1 = std::async(download_worker, ...)
Thread W2 = std::async(download_worker, ...)
```

#### Inside each worker thread

```
Worker loop:

1. Lock DownloadState::mtx
2. If pending is empty -> break (worker is done)
3. Pop chunk_idx from front of pending queue
4. Look up chunk_availability[chunk_idx]
5. Pick random peer using thread_local mt19937
6. Unlock mtx

7. Call download_chunk_to_fd(peer, "movie.mp4", chunk_idx, fd, piece_hash[chunk_idx])
   (This happens WITHOUT holding the lock — all workers download simultaneously)

8a. If SUCCESS:
      chunks_done++ (atomic)
      Print progress under log_mutex: "\rProgress: 5/20 chunks (25%)"
      -> Go to step 1

8b. If FAILURE:
      Lock mtx
      Remove failed peer from chunk_availability[chunk_idx]
      If other peers still available:
        push chunk_idx back to pending  <- another worker will retry it
      Else:
        fatal_error = true              <- no peer can serve this chunk
      Unlock mtx
      -> Go to step 1 (or exit if fatal_error)
```

#### Inside `download_chunk_to_fd` — the actual chunk transfer

```
CLIENT WORKER THREAD                      PEER (e.g. Alice)
  |                                               |
  |  create_socket()                              |
  |  connect_to_server(alice_ip, alice_port)      |
  |                                               |
  |  [framed] "GET_CHUNK$$movie.mp4$$7"           |
  |---------------------------------------------> |
  |                                               |
  |                      open file at shared_files["movie.mp4"]
  |                      seekg(7 * 524288)  = offset 3670016
  |                      read up to 524288 bytes
  |                      bytes_read = 524288 (or less for last chunk)
  |                                               |
  |  [framed] "OK$$524288"                        |
  |<--------------------------------------------- |
  |                                               |
  |  [framed] "READY"                             |
  |---------------------------------------------> |
  |                                               |
  |  [RAW - NOT FRAMED] 524288 bytes of data      |
  |<--------------------------------------------- |
  |                                               |
  |  close socket                                 |
  |                                               |
  |  Verify: sha1_hash(received_data) == piece_hash_list[7]
  |    MATCH:   pwrite(fd, data, 524288, 7 * 524288)  <- write to disk at correct offset
  |    MISMATCH: return false -> worker blacklists Alice for chunk 7
```

**Key point about `pwrite`:**
Worker 1 might be writing chunk 3 at offset 1572864 while Worker 2 writes chunk 11 at
offset 5767168 at exactly the same moment. Because these byte ranges do not overlap and
`pwrite` is POSIX-atomic per call, there is no data corruption. No mutex is needed for the
file write.

### Step 7 — Post-download verification

After all workers finish and `fd` is closed:

```cpp
if (state.fatal_error || state.chunks_done < num_chunks) {
    // Download failed — return false
    // Caller deletes the partial file
}

// Recompute the file's fingerprint from the piece hashes we received
string actual_filehash = sha256_hash(join_string(piece_hash_list, ","));

if (actual_filehash != expected_filehash) {
    // The tracker's piece hash list was tampered with or corrupted
    // Delete the file — do NOT register as seeder
    remove(dest_path);
    return;
}
```

This final check ensures end-to-end integrity: even if every individual chunk passed SHA1
verification, the master hash confirms the entire piece hash list came from the original
uploader.

### Step 8 — Register as seeder

Only NOW, after the file is verified on disk, does Bob tell the tracker he is a seeder:

```
CLIENT                                                  TRACKER
  |                                                        |
  |  [framed] "register_seeder cs_students movie.mp4"     |
  |------------------------------------------------------> |
  |                                                        |
  |                          validate: group exists?
  |                          validate: bob is a member?
  |                          validate: movie.mp4 exists in group?
  |                          file_seeders["cs_students"]["movie.mp4"].insert("bob")
  |                                                        |
  |  [framed] "OK$$Registered as seeder"                   |
  |<------------------------------------------------------ |
```

### Step 9 — Client local state update

```cpp
lock file_mutex
  shared_files["movie.mp4"]  = "/home/bob/downloads/movie.mp4"
  file_chunks["movie.mp4"]   = [true, true, ..., true]   // all 20 chunks
  file_hashes["movie.mp4"]   = piece_hash_list
unlock

downloaded_files["movie.mp4"] = "cs_students"
```

Bob is now a full seeder. His peer server will immediately serve any chunk of `movie.mp4` to
other peers who ask.

### Download Flow Summary Diagram

```
User: "download_file cs_students movie.mp4 /home/bob/downloads"
    |
    v
[Validate dest dir exists, file not already there]
    |
    v
[Send download_file command to tracker]
    |
    v
[Tracker validates membership + file exists]
[Tracker builds active seeder list (logged-in users with known addresses)]
[Tracker sends SEEDER_LIST with: seeders, filesize, piece_hashes, filehash]
    |
    v
[PHASE 1 - Parallel bitvector queries]
[One std::async thread per seeder, all fire simultaneously]
[Each thread: connect -> GET_BITVECTOR -> receive "11001..." -> close]
[Build chunk_availability map: chunk_i -> [peers who have it]]
    |
    v
[Verify every chunk has at least one source]
    |
    v
[Pre-allocate destination file to full size on disk]
[Open raw fd with open(O_RDWR)]
    |
    v
[PHASE 2 - Parallel chunk download]
[Build DownloadState: pending queue = {0,1,2,...,N-1}]
[Spawn min(seeders, 4, N) worker threads via std::async]
    |
    |-- Worker 1: pop chunk -> connect to peer -> download -> SHA1 verify -> pwrite
    |-- Worker 2: pop chunk -> connect to peer -> download -> SHA1 verify -> pwrite
    |-- Worker 3: pop chunk -> connect to peer -> download -> SHA1 verify -> pwrite
    |-- Worker 4: pop chunk -> connect to peer -> download -> SHA1 verify -> pwrite
    |      (failed chunks re-queued with that peer blacklisted for that chunk)
    |
    v
[Wait for all workers: for (auto& f : futures) f.get()]
[Close fd]
    |
    v
[Verify whole-file: sha256(joined_piece_hashes) == expected_filehash]
[MISMATCH: delete file, return error]
    |
    v
[Send "register_seeder cs_students movie.mp4" to tracker]
    |
    v
[Update local state: shared_files, file_chunks, file_hashes]
    |
    v
DONE — Bob is now a seeder and his peer server serves the file to others
```

---

## 11. Parallel Download Deep Dive

### Why Parallel Downloads Are Faster

Suppose Bob is downloading a 20-chunk file from 2 seeders (Alice and Charlie). Each chunk
download takes 1 second.

**Old sequential approach:**
```
Time:    0s  1s  2s  3s  4s  5s  6s  7s  8s  9s  10s  ...  20s
Action:  [0] [1] [2] [3] [4] [5] [6] [7] [8] [9] [10] ... [19]
         Alice           Alice           Charlie         Alice
Total time: 20 seconds. Only 1 peer busy at a time.
```

**New parallel approach (2 workers):**
```
Time:    0s         1s         2s         3s         ...  10s
Worker1: [chunk 0]  [chunk 2]  [chunk 4]  [chunk 6]  ... [chunk 18]
Worker2: [chunk 1]  [chunk 3]  [chunk 5]  [chunk 7]  ... [chunk 19]
Total time: 10 seconds. Both peers fully utilized.
```

With 4 workers and 4 seeders, it would take ~5 seconds.

### Failure and Retry Handling

```
Example: Worker 1 tries to download chunk 5 from Alice, but Alice's connection drops.

1. download_chunk_to_fd returns false
2. Worker 1 acquires state.mtx
3. chunk_availability[5] = ["Alice", "Charlie"]
4. Remove "Alice":  chunk_availability[5] = ["Charlie"]
5. "Charlie" is still available -> push 5 back to pending queue
6. Release lock
7. Worker 1 loops back, pops chunk 5 again
8. This time picks "Charlie" (only option)
9. Downloads successfully from Charlie

If Alice was the ONLY peer for chunk 5:
3. chunk_availability[5] = ["Alice"]
4. Remove "Alice":  chunk_availability[5] = []  (empty)
5. state.fatal_error = true
6. All workers see fatal_error on their next loop check and exit
7. download_file_from_peers returns false
8. Caller deletes the partial file
```

### Thread Safety Map

| Resource             | Protection Mechanism  | Reason                                       |
|----------------------|-----------------------|----------------------------------------------|
| `pending` queue      | `DownloadState::mtx`  | Complex structure, non-atomic pop             |
| `chunk_availability` | `DownloadState::mtx`  | Vector mutation (erase) is not atomic         |
| `chunks_done`        | `atomic<int>`         | Single integer, atomic increment is enough    |
| `fatal_error`        | `atomic<bool>`        | Single boolean, atomic write/read is enough   |
| `stdout` progress    | `log_mutex`           | Prevents interleaved `\r` lines               |
| File writes          | None needed           | `pwrite()` is atomic for non-overlapping regions |
| `shared_files` etc.  | `file_mutex`          | Final state update at end, held briefly       |

---

## 12. Peer-to-Peer Chunk Serving

When Bob (or any client) receives an incoming connection on his peer server port:

```
run_peer_server():
  server_socket = create_socket()
  bind_and_listen(my_ip, my_port)
  while (server_running):
    peer_socket = accept()
    thread(handle_peer_request, peer_socket).detach()
```

### handle_peer_request — Two Commands

**GET_BITVECTOR:**
```
Receive: [framed] "GET_BITVECTOR$$movie.mp4"

lock file_mutex
  check file_chunks["movie.mp4"] exists
  build string: for each bool in file_chunks["movie.mp4"]:
                  bitvector += (has_chunk ? "1" : "0")
unlock

Send: [framed] "OK$$11111011101..." (1=have chunk, 0=missing)
close socket
```

**GET_CHUNK:**
```
Receive: [framed] "GET_CHUNK$$movie.mp4$$7"

lock file_mutex
  filepath = shared_files["movie.mp4"]
unlock

open(filepath, binary)
seekg(7 * 524288)           <- offset of chunk 7
read up to 524288 bytes
bytes_read = actual bytes read (may be less for last chunk)
close file

Send: [framed] "OK$$524288"      <- tells downloader how many bytes to expect

Receive: [framed] "READY"        <- downloader's acknowledgment

Send: [RAW - 524288 bytes]       <- binary chunk data, NOT framed
                                    (length was already negotiated above)
close socket
```

Each connection handles exactly one request then closes. The downloader opens a fresh
TCP connection for each chunk and for each bitvector query. This is simple and correct;
performance is acceptable because TCP connection overhead is small compared to chunk
transfer time.

---

## 13. Data Structures Reference

### Tracker Data Structures (`Tracker_Master.cpp`)

```
users:           map<string, string>
                 username -> password (plain text — should be hashed)

user_logged_in:  map<string, bool>
                 username -> is currently logged in
                 Set to false on disconnect even without explicit logout

user_address:    map<string, string>
                 username -> "ip:port" of their peer server
                 Only valid when user_logged_in[username] == true

group_admin:     map<string, string>
                 group_id -> admin username
                 A group exists iff it has an entry here

group_members:   map<string, set<string>>
                 group_id -> set of usernames who are full members

group_pending:   map<string, set<string>>
                 group_id -> set of usernames with pending join requests

file_seeders:    map<string, map<string, set<string>>>
                 group_id -> filename -> set of usernames who have the full file
                 Only users in this set after confirming download via register_seeder

file_sizes:      map<string, string>
                 filename -> filesize as string (e.g. "10485760")
                 NOTE: keyed by filename only — conflict if two groups share filename

piece_hashes:    map<string, string>
                 filename -> "hash0,hash1,hash2,..." (SHA1 of each 512KB chunk)

file_hashes:     map<string, string>
                 filename -> sha256(piece_hashes string) — whole-file fingerprint

data_mutex:      mutex — guards all of the above maps
```

### Client Data Structures (`Client_Master.cpp`)

```
my_ip / my_port:    string / int — this peer's listening address

tracker_ip / port:  string / int — tracker's address

is_logged_in:       atomic<bool> — login state (atomic: read by peer server thread too)
current_user:       string — username of logged-in user

shared_files:       map<string, string>
                    filename -> absolute filepath on disk
                    Peer server looks here to open files for GET_CHUNK

file_chunks:        map<string, vector<bool>>
                    filename -> bitvector (true = have this chunk)
                    Peer server looks here for GET_BITVECTOR
                    All true after upload or successful download

file_hashes:        map<string, vector<string>>
                    filename -> per-chunk SHA1 hash list

downloaded_files:   map<string, string>
                    filename -> group_id (for show_downloads display)

file_mutex:         mutex — guards shared_files, file_chunks, file_hashes

server_running:     atomic<bool> — signals peer server thread to stop on quit
```

### Parallel Download State (`Client_Master.cpp`)

```
DownloadState (per active download — stack allocated, not global):

  mtx:                mutex — protects pending and chunk_availability
  pending:            queue<int> — chunk indices not yet downloaded
  chunk_availability: map<int, vector<string>> — per chunk, list of peer addresses
                      (peers are removed from this list on failure)
  chunks_done:        atomic<int> — successfully downloaded + verified chunk count
  fatal_error:        atomic<bool> — set when a chunk has no remaining peers
  num_chunks:         int — total chunks in this file
```

---

## 14. Full Command & Protocol Reference

### Tracker Commands (client -> tracker, over persistent TCP connection)

All messages use length-prefix framing. Request format is space-separated text.
Response format is `status$$message` where status is `OK` or `ERROR:`.

```
+------------------+----------------------------------+----------------------------------------+
| Command          | Request Format                   | Response                               |
+------------------+----------------------------------+----------------------------------------+
| create_user      | create_user <user> <pass>        | OK$$Account created successfully       |
| login            | login <user> <pass>              | OK$$LOGIN_OK  (then client sends addr) |
|                  |                                  | -> OK$$Login successful                |
| logout           | logout                           | OK$$Logged out successfully            |
| create_group     | create_group <group_id>          | OK$$Group created successfully         |
| join_group       | join_group <group_id>            | OK$$Join request sent                  |
| leave_group      | leave_group <group_id>           | OK$$Left group successfully            |
| list_groups      | list_groups                      | OK$$grp1,grp2,grp3                     |
| list_requests    | list_requests <group_id>         | OK$$user1,user2                        |
| accept_request   | accept_request <gid> <uid>       | OK$$Request accepted                   |
| change_admin     | change_admin <gid> <new_admin>   | OK$$Admin changed successfully         |
| list_files       | list_files <group_id>            | OK$$file1,file2,file3                  |
| upload_file      | upload_file <path> <group_id>    | OK$$SEND_FILE_DETAILS                  |
|                  |   -> client sends file details   | -> OK$$File uploaded successfully      |
| download_file    | download_file <gid> <fname> <dst>| OK$$SEEDER_LIST$$...                   |
| register_seeder  | register_seeder <gid> <fname>    | OK$$Registered as seeder               |
| stop_share       | stop_share <gid> <fname>         | OK$$Stopped sharing file               |
+------------------+----------------------------------+----------------------------------------+
```

### Upload File Details Message (client -> tracker, immediately after SEND_FILE_DETAILS)

```
Format:  filename$$filesize$$filehash$$piece_hashes
Example: movie.mp4$$10485760$$<sha256_hex>$$<sha1_0>,<sha1_1>,...,<sha1_19>

- filename:     base filename only (no path)
- filesize:     file size in bytes as decimal string
- filehash:     sha256_hex(all_piece_hashes_joined_with_comma)
- piece_hashes: sha1_hex(chunk_0),sha1_hex(chunk_1),...  (each chunk is 512KB)
```

### Download File Response (tracker -> client)

```
Format:  OK$$SEEDER_LIST$$seeders$$filesize$$piece_hashes$$filehash
Example: OK$$SEEDER_LIST$$192.168.1.5:6001,192.168.1.6:6002$$10485760$$<sha1_0>,...$$<sha256>

parts[0] = "OK"
parts[1] = "SEEDER_LIST"
parts[2] = comma-separated "ip:port" of active seeders
parts[3] = filesize in bytes
parts[4] = comma-separated SHA1 piece hashes
parts[5] = SHA256 whole-file hash (sha256 of the joined piece hashes string)
```

### Peer-to-Peer Protocol (per-connection, client <-> client)

```
+---------------+-------------------------------+--------------------------------+
| Command       | Request (framed)              | Response                       |
+---------------+-------------------------------+--------------------------------+
| GET_BITVECTOR | GET_BITVECTOR$$<filename>     | OK$$11101010...                |
|               |                               | (1=have chunk, 0=missing)      |
| GET_CHUNK     | GET_CHUNK$$<filename>$$<num>  | OK$$<chunk_size_in_bytes>      |
|               |   then client sends:          |   then server sends:           |
|               |   READY  (framed)             |   <raw binary bytes> (NOT      |
|               |                               |    framed - length negotiated) |
+---------------+-------------------------------+--------------------------------+
```

### Message Framing Wire Format

```
Every send_data() / recv_data() call sends/receives exactly:

Bytes 0-3:    uint32_t body length in network byte order (big-endian)
Bytes 4-N+3:  UTF-8 message body (N bytes)

Example: sending "OK$$Hello"  (9 bytes)

On the wire: [0x00][0x00][0x00][0x09][O][K][$$][H][e][l][l][o]
              ^-- htonl(9) --^  ^------- 9 bytes of body ------^

The raw binary chunk transfer (after READY) does NOT use this framing.
```

---

## Summary

This system implements a complete BitTorrent-like P2P file sharing architecture with
group-based access control. The key design properties:

| Property             | Mechanism                                                         |
|----------------------|-------------------------------------------------------------------|
| Access control       | Groups with admin-approved membership                             |
| Chunk integrity      | SHA1 verification of every 512 KB piece before writing to disk    |
| File integrity       | SHA256 of all piece hashes verified after full download           |
| Seeder accuracy      | `register_seeder` sent only after verified complete download      |
| Parallel download    | Up to 4 worker threads drain a shared chunk queue simultaneously  |
| Thread-safe writes   | `pwrite()` with non-overlapping offsets — no mutex on file writes |
| Protocol reliability | 4-byte length-prefix framing — no message truncation or merging   |
| Peer selection       | Per-thread `mt19937` RNG — thread-safe, non-predictable           |
| Failure recovery     | Failed peers blacklisted per-chunk; chunk re-queued for retry     |
