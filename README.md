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
./dataServer -p <port>
```

### Running the client

```bash
cd client
./remoteClient -i <server_ip> -p <server_port>
```

- The server treats `server/start_files` as its current working directory for tranfers.

## TODO

- [] Make it automatic:
  - [X] Send files from client to server (use event loop)
  - [] Compare files from client to files on server and exchange the missing/modified ones with each other
  - [X] D U P A - delete useless parameters asap
  - [] Resolve conflicts - end task
