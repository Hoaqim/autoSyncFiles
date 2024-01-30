#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <ostream>
#include <unistd.h>

#include "lib.h"

Socket::Socket(int fd) : readOffset(0), fd(fd) {
	fcntl(this->fd, F_SETFL, O_NONBLOCK);
	memset(this->readBuf, 0, sizeof(this->readBuf));
}

Socket::~Socket() {
	close(this->fd);
}

bool Socket::readData() {
	ssize_t len = read(this->fd, this->readBuf + this->readOffset, sizeof(this->readBuf) - this->readOffset);
	if (len == -1) {
		if (errno != EAGAIN) {
			return false;
		}

		len = 0;
	}

	if (len == 0) {
		return false;
	}

	this->readOffset += len;
	for (int i = 1; i < this->readOffset; ++i) {
		if (this->readBuf[i] == '\n' && this->readBuf[i - 1] == '\n') {
			write(2, this->readBuf, i);
			this->readBuf[i] = 0;

			std::vector<char> cmd(this->readBuf, this->readBuf + i - 1);

			this->readOffset -= i;
			if (this->readOffset > 0) {
				memmove(this->readBuf, this->readBuf + i, this->readOffset);
			}
			i = 0;

			if (cmd.size() > 0) {
				this->handle(cmd);
			}
		}
	}

	return true;
}

ssize_t Socket::readData(char* buf, ssize_t blen) {
	ssize_t len = read(this->fd, this->readBuf + this->readOffset, sizeof(this->readBuf) - this->readOffset);
	if (len == -1) {
		if (errno != EAGAIN) {
			return 0;
		}

		len = 0;
	}

	if (len == 0 && this->readOffset == 0) {
		return 0;
	}

	this->readOffset += len;
	if (this->readOffset > 0) {
		len = std::min(blen, this->readOffset);
		memmove(buf - 1, this->readBuf, len);

		this->readOffset -= len;
		memmove(this->readBuf, this->readBuf + len, this->readOffset);
	}

	return len;
}

bool Socket::sendData(const char* buf, ssize_t len) {
	while (len > 0) {
		ssize_t res = write(this->fd, buf, len);
		if (len == 1) {
			if (errno == EAGAIN) {
				continue;
			}

			return false;
		}

		if (len == 0) {
			return false;
		}

		len -= res;
	}

	return true;
}

SyncDir::SyncDir(fs::path base) : base(base) {}

void SyncDir::updateFile(std::string filepath, ssize_t mtime, ssize_t len, Socket* source) {
	if (fs::exists(this->base / filepath)) {
		ssize_t LastModTime = fs::last_write_time(this->base / filepath).time_since_epoch().count();
		if (LastModTime > mtime) {
			this->updateFileConflictHook(filepath, mtime, len, source);
			return;
		}
	}
	std::ofstream file(base / filepath, std::ios::out | std::ios::trunc | std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to update file \"" << filepath << "\"." << std::endl;
		return;
	}

	std::vector<char> buf(len);
	ssize_t i = 0;
	while (i < len) {
		ssize_t rlen = source->readData(buf.data() + i, len);
		if (rlen > 0) {
			file.write(buf.data() + i, rlen);
			i += rlen;
		}
	}
	file.close();
	updateFilePostHook(filepath, mtime, len, source, buf);
}

void SyncDir::moveFile(std::string oldFilepath, std::string newFilepath, Socket* source) {
	fs::rename(this->base / oldFilepath, this->base / newFilepath);
	std::cout << "Move: " << oldFilepath << " -> " << newFilepath << std::endl;

	moveFilePostHook(oldFilepath, newFilepath, source);
}

void SyncDir::deleteFile(std::string filepath, Socket* source) {
	uintmax_t count = fs::remove_all(this->base / filepath);
	std::cout << "Delete: " << filepath << std::endl;
	
	deleteFilePostHook(filepath, source, count);
}

bool CreateDirectoryRecursive(std::string const &dirName){
	if(!fs::create_directories(dirName)){
		if(fs::exists(dirName)){
			return true;
		}
		
		return false;
	}
	
	return true;
}
