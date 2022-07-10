# Bit-Torrent Peer-to-Peer Group Based File Sharing System

## Prerequisites

- **Install OpenSSL library :** `sudo apt-get install openssl`

## How to Run

### Start Tracker


```
g++ Tracker_Master.cpp -o Tracker_Master -lssl -lcrypto -pthread
./Tracker_Master "Tracker_Info_File_Name" "Tracker_NO( 1 or 2 )"
ex: ./Tracker_Master tracker_info.txt 1
```

### Start Client:

```
g++ Client_Master.cpp -o Client_Master -lssl -lcrypto -pthread
./Client_Master "IP:PORT" "Tracker_Info_File_Name"
ex: ./Client_Master 127.0.0.1:18000 tracker_info.txt
```

## IMP Points

1. Single tracker is implemented. It should always be running.
2. Random Piece Selection and Random peer selection Algorithm for that piece is Used for downloading chunks of file from peer.
3. Admin of any group should change the admin to some another member of that group before leaving it.
4. Downloading and uploading path should be absolute.  


## Commands

1. Create user account:

```
create_user​ "user_id" "password"
```

2. Login:

```
login​ "user_id" "password"
```

3. Create Group:

```
create_group​ "group_id"
```

4. Join Group:

```
join_group​ "group_id"
```

5. Leave Group:

```
leave_group​ "group_id"
```

6. List pending requests:

```
list_requests ​"group_id"
```

7. Accept Group Joining Request:

```
accept_request​ "group_id" "user_id"
```

8. List All Group In Network:

```
list_groups
```

9. List All sharable Files In Group:

```
list_files​ "group_id"
```

10. Upload File:

```
​upload_file​ "file_path" "group_id​"
```

11. Download File:​

```
download_file​ "group_id" "file_name" "destination_path"
```

12. Logout:​

```
logout
```

13. Show_downloads: ​

```
show_downloads
```

14. Stop sharing: ​

```
stop_share ​"group_id" "file_name"
```

15. Change Admin:
```
change_admin "group_id" "new_admin_uid"
```

