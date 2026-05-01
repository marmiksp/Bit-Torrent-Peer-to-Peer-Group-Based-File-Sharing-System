# P2P File Sharing System (BitTorrent-style)

A peer-to-peer file sharing system with group-based access control.

## Features

- Group-based sharing with admin controls
- Chunk-based file transfer (512KB chunks)
- Multi-peer parallel downloads
- SHA1 hash verification for integrity
- Network-ready (runs on different machines)

## Prerequisites

```bash
sudo apt-get install libssl-dev g++
```

## Building

```bash
# Compile Tracker
g++ -std=c++17 Tracker_Master.cpp -o tracker -lssl -lcrypto -pthread

# Compile Client
g++ -std=c++17 Client_Master.cpp -o client -lssl -lcrypto -pthread
```

## Running

### Start Tracker
```bash
./tracker <tracker_ip> <tracker_port>
# Example: ./tracker 192.168.1.100 5000
```

### Start Client
```bash
./client <client_ip> <client_port> <tracker_ip> <tracker_port>
# Example: ./client 192.168.1.101 6000 192.168.1.100 5000
```

### Local Testing
```bash
# Terminal 1: Tracker
./tracker 127.0.0.1 5000

# Terminal 2: Client 1
./client 127.0.0.1 6001 127.0.0.1 5000

# Terminal 3: Client 2
./client 127.0.0.1 6002 127.0.0.1 5000
```

## Commands

### User Management
- `create_user <username> <password>`
- `login <username> <password>`
- `logout`

### Group Management
- `create_group <group_id>`
- `join_group <group_id>`
- `leave_group <group_id>`
- `list_groups`
- `list_requests <group_id>` (admin only)
- `accept_request <group_id> <user_id>` (admin only)
- `change_admin <group_id> <user_id>`

### File Operations
- `upload_file <filepath> <group_id>`
- `download_file <group_id> <filename> <dest_path>`
- `list_files <group_id>`
- `stop_share <group_id> <filename>`
- `show_downloads`

### Other
- `help`
- `quit`

## Example Session

```bash
# Alice creates group and uploads file
>> create_user alice pass123
>> login alice pass123
>> create_group movies
>> upload_file /home/alice/video.mp4 movies

# Bob joins and downloads
>> create_user bob pass456
>> login bob pass456
>> join_group movies

# Alice accepts Bob
>> accept_request movies bob

# Bob downloads
>> download_file movies video.mp4 /home/bob/downloads
```

## Technical Details

- Chunk Size: 512 KB
- Hash: SHA1 (chunks), SHA256 (file)
- Protocol: TCP with send/recv
- Threading: Multi-threaded server and downloads

## License

MIT License
