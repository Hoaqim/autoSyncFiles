#pragma once
#include <vector>
#include <filesystem>
#include <linux/limits.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

constexpr int MAX_EVENTS = 10;
constexpr int BUFFER_SIZE = PATH_MAX;

namespace fs = std::filesystem;

class Socket {
	char readBuf[BUFFER_SIZE];
	ssize_t readOffset;

public:
	int fd;
	
	Socket(int fd);
	virtual ~Socket();
	virtual void handle(std::vector<char>) = 0;
	bool readData();
	ssize_t readData(char* buf, ssize_t len);
	bool sendData(const char* buf, ssize_t len);
	inline bool sendData(std::string buf) {
		return this->sendData(buf.data(), buf.size());
	};
};

class SyncDir {
public:
	fs::path base;

	SyncDir(fs::path base);
	
	void updateFile(std::string filepath, ssize_t mtime, ssize_t len, Socket* source);
	[[gnu::noinline]]
	virtual void updateFileConflictHook(
		[[maybe_unused]] std::string filepath,
		[[maybe_unused]] ssize_t mtime,
		[[maybe_unused]] ssize_t len,
		[[maybe_unused]] Socket* source
	) {}
	[[gnu::noinline]]
	virtual void updateFilePostHook(
		[[maybe_unused]] std::string filepath,
		[[maybe_unused]] ssize_t mtime,
		[[maybe_unused]] ssize_t len,
		[[maybe_unused]] Socket* source,
		[[maybe_unused]] char* buf
	) {}

	void moveFile(std::string oldFilepath, std::string newFilepath, Socket* source);
	[[gnu::noinline]]
	virtual void moveFilePostHook(
		[[maybe_unused]] std::string oldFilepath,
		[[maybe_unused]] std::string newFilepath,
		[[maybe_unused]] Socket* source
	) {}

	void deleteFile(std::string filepath, Socket* source);
	[[gnu::noinline]]
	virtual void deleteFilePostHook(
	  [[maybe_unused]] std::string filepath,
	  [[maybe_unused]] Socket* source,
	  [[maybe_unused]] uintmax_t count
	) {}
};

bool CreateDirectoryRecursive(std::string const &dirName);
