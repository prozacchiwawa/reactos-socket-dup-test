#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <exception>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

class WSAStarter {
public:
	WSAStarter() {
		WSAStartup(0x0202, &wsd);
	}

	~WSAStarter() {
		WSACleanup();
	}
	
	WSADATA wsd;
};

class WinError : public std::exception {
public:
	WinError(const std::string &text, int err) :
		text(text), err(err) { }

	std::string text;
	int err;
};

class Socket {
public:
	Socket() : h(INVALID_SOCKET) { }
	Socket(SOCKET s) : h(s) { }

	~Socket() {
		if (h != INVALID_SOCKET) {
			closesocket(h);
			h = INVALID_SOCKET;
		}
	}

    void bind() {
		struct sockaddr_in sa = { };
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (::bind(h, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
			throw new WinError("bind", WSAGetLastError());
		}
	}

	sockaddr_in getsockname() {
		struct sockaddr_in sa = { };
		int salen = sizeof(sa);
		sa.sin_family = AF_INET;
		if (::getsockname(h, (struct sockaddr *)&sa, &salen) == -1) {
			throw new WinError("getsockname", WSAGetLastError());
		}
		return sa;
	}

	void connect(const sockaddr_in &sa) {
		if (::connect(h, (const struct sockaddr *)&sa, sizeof(sa)) == -1) {
			throw new WinError("connect", WSAGetLastError());
		}
	}

	void listen() {
		if (::listen(h, 5) == -1) {
			throw new WinError("listen", WSAGetLastError());
		}
	}

	SOCKET accept() {
		SOCKET s = ::accept(h, NULL, NULL);
		if (s != INVALID_SOCKET) {
			return s;
		}
		throw WinError("accept", WSAGetLastError());
	}

	void write(const std::string &s) {
		int offset = 0;
		int size = s.size();
		std::vector<char> buf(size + sizeof(size), 0);
		memcpy(&buf[0], &size, sizeof(size));
		memcpy(&buf[sizeof(size)], s.c_str(), size);
		while (offset < buf.size()) {
			int send_res = ::send(h, &buf[0] + offset, buf.size() - offset, 0);
			if (send_res == -1) {
				throw WinError("send", WSAGetLastError());
			}
			offset += send_res;
		}
	}

	std::string read() {
		int len = 0;
		int offset = 0;
		while (offset < sizeof(len)) {
			int recv_res = ::recv(h, ((char *)&len) + offset, sizeof(len) - offset, 0);
			if (recv_res == -1) {
				throw WinError("recv", WSAGetLastError());
			}
			if (recv_res == 0) {
				return "";
			}
			offset += recv_res;
		}
		std::vector<char> buf(len, 0);
		offset = 0;
		while (offset < buf.size()) {
			int recv_res = ::recv(h, &buf[0] + offset, buf.size() - offset, 0);
			if (recv_res == -1) {
				throw WinError("recv", WSAGetLastError());
			}
			if (recv_res == 0) {
				return "";
			}
			offset += recv_res;
		}
		return std::string(&buf[0], buf.size());
	}

	void shutdown(int how) {
		if (::shutdown(h, how) == -1) {
			throw WinError("shutdown", WSAGetLastError());
		}
	}

	void dup(DWORD processId, void *protocol_info) const {
		if (WSADuplicateSocket(h, processId, (LPWSAPROTOCOL_INFO)protocol_info) == SOCKET_ERROR) {
			throw WinError("WSADuplicateSocket", WSAGetLastError());
		}
	}

private:
	Socket(const Socket &other);
	SOCKET h;
};

class Mutex {
public:
	Mutex() : h(INVALID_HANDLE_VALUE) { }
	~Mutex() {
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
	}

	void create(const std::string &name) {
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
		h = CreateMutex(NULL, TRUE, name.c_str());
		if (!h) {
			h = INVALID_HANDLE_VALUE;
			throw WinError("CreateMutex", GetLastError());
		}
	}

	void open(const std::string &name) {
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
		h = OpenMutex(SYNCHRONIZE, FALSE, name.c_str());
		if (!h) {
			h = INVALID_HANDLE_VALUE;
			throw WinError("OpenMutex", GetLastError());
		}
	}

	void wait() {
		if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) {
			throw WinError("WaitForSingleObject", GetLastError());
		}
	}

	void release() {
		ReleaseMutex(h);
	}

private:
	HANDLE h;
};

class SharedMem {
public:
	SharedMem() : h(INVALID_HANDLE_VALUE), mem() { }
	~SharedMem() {
		if (mem) {
		    UnmapViewOfFile(mem);
			mem = nullptr;
		}
	    if (h != INVALID_HANDLE_VALUE) {
		    CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
	}

	void create(const std::string &name, int size) {
	    if (mem) {
		    UnmapViewOfFile(mem);
			mem = nullptr;
		}
		if (h != INVALID_HANDLE_VALUE) {
		    CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
		h = CreateFileMapping
		    (INVALID_HANDLE_VALUE,
			 nullptr,
			 PAGE_READWRITE | SEC_COMMIT,
			 0,
			 (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
			 name.c_str());

		if (!h) {
		    h = INVALID_HANDLE_VALUE;
			throw WinError("CreateFileMapping", GetLastError());
		}
	}

	void open(const std::string &name) {
	    if (mem) {
		    UnmapViewOfFile(mem);
			mem = nullptr;
		}
		if (h != INVALID_HANDLE_VALUE) {
		    CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
		h = OpenFileMapping
		    (FILE_MAP_ALL_ACCESS,
			 FALSE,
			 name.c_str());

		if (!h) {
		    h = INVALID_HANDLE_VALUE;
			throw WinError("OpenFileMapping", GetLastError());
		}
	}

    void map(int size) {
		if (mem) {
		    return;
		}

		if (h != INVALID_HANDLE_VALUE) {
		    mem = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
			if (!mem) {
			    throw WinError("MapViewOfFile", GetLastError());
			}
		}
	}

    void *get() { return mem; }
	HANDLE getHandle() const { return h; }

private:
	void *mem;
	HANDLE h;
};

void PassToSubprocess(Mutex &m, SharedMem &sm, const Socket &o) {
    STARTUPINFO si = { };
	PROCESS_INFORMATION pi = { };
	
	std::ostringstream shm_name;
	shm_name << "Local\\shm" << (int)GetCurrentProcessId();
	std::ostringstream mutex_name;
	mutex_name << "Local\\mutex" << (int)GetCurrentProcessId();
	
	std::ostringstream oss;
	oss << "duptest.exe " << shm_name.str() << " " << mutex_name.str();

	std::cout << "Subprocess: " << oss.str() << "\n";
	
	sm.create(shm_name.str(), PAGE_SIZE);
	m.create(mutex_name.str());
	
	if (!CreateProcess
		(NULL,
		 (char*)oss.str().c_str(),
		 NULL,
		 NULL,
		 FALSE,
		 0,
		 NULL,
		 NULL,
		 &si,
		 &pi)) {
		throw WinError("CreateProcess", GetLastError());
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	sm.map(PAGE_SIZE);
	o.dup(pi.dwProcessId, sm.get());
	m.release();
}

int main(int argc, char **argv) {
	WSAStarter _starter;
	try {
		if (argc != 1) {
			std::string shm_name(argv[1]);
			std::string mutex_name(argv[2]);
			std::cout << "shm " << shm_name << " mutex " << mutex_name << "\n";
			
			Mutex m;
			m.open(mutex_name);
			m.wait();
			
			SharedMem sm;
			sm.open(shm_name);
			sm.map(PAGE_SIZE);

			Socket d(WSASocket(AF_INET, SOCK_STREAM, 0, (LPWSAPROTOCOL_INFO)sm.get(), 0, 0));
			std::cout << "SUBP " << d.read() << "\n";
			d.write("We are devo");
			d.shutdown(2);
		} else {
			Socket s(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        	Socket o(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        	s.bind();
			s.listen();
			auto sa = s.getsockname();
			o.connect(sa);
			Socket a(s.accept());
			Mutex m;
			SharedMem sm;
			PassToSubprocess(m, sm, o);
			a.write("Are we not men?");
			std::cout << "PARP " << a.read().c_str() << "\n";
		}
    } catch (const WinError &we) {
		std::cerr << "Exception thrown: " << we.err << ": " << we.text.c_str() << "\n";
	}
}
