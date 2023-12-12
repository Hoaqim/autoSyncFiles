## Usage

```bash
# Compile the project
make

# Cleanup
make clean
```

### Running the server

```bash
cd server
./dataServer -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size>
```

### Running the client

```bash
cd client
./remoteClient -i <server_ip> -p <server_port> -d <directory>
```

- The server treats `server/start_files` as its current working directory for tranfers.

## TODO

- [x] Make it automatic:
  - [x] Send files from client to server (use event loop)
  - [x] Compare files from client to files on server and exchange the missing/modified ones with each other
  - [x] Don't store the files locally on structure (Make a simple struct)
  - [x] D U P A
  - [x] Resolve conflicts - end task
- [x] Send messages to client - optional
