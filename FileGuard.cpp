#include "FileGuard.h"
#include <Windows.h>
#include <io.h>

#if defined(_DEBUG)
#define print(fmt, ...)\
do { \
	char buffer[512] = { 0 };\
	snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__);\
	printf(fmt, ##__VA_ARGS__);\
	OutputDebugString(buffer);\
}while(0);
#else
#define print(fmt, ...)
#endif

static std::string unicode2ansi(const std::wstring& str)
{
	int size = WideCharToMultiByte(CP_ACP, 0, str.c_str(), -1, 0, 0, 0, 0);
	std::unique_ptr<char> buffer(new char[size]);
	memset(buffer.get(), 0, size);
	WideCharToMultiByte(CP_ACP, 0, str.c_str(), -1, buffer.get(), size, 0, 0);
	return std::string(buffer.get());
}

const char* const FileGuard::ALL_DISK_PATHS = "*";

const char* const FileGuard::EXCEPT_SYSTEM_DISK_PATHS = "&";

const char* const FileGuard::ALL_SUFFIXES = ".*";

FileGuard::FileGuard()
{

}

FileGuard::~FileGuard()
{
	stop();
	clearPaths();
}

bool FileGuard::existPath(const std::string& path) const
{
	bool exist = false;
	for (const auto& x : m_args)
	{
		if (x.path == path)
		{
			exist = true;
			break;
		}
	}
	return exist;
}

bool FileGuard::addPath(const std::string& path, bool subpath)
{
	bool result = false, success = true;
	do
	{
		if (_access(path.c_str(), 0) == -1 && path != ALL_DISK_PATHS && path != EXCEPT_SYSTEM_DISK_PATHS) {
			setLastError("%s路径不存在", path.c_str());
			break;
		}

		std::vector<std::string> paths;
		if (path == ALL_DISK_PATHS || path == EXCEPT_SYSTEM_DISK_PATHS) {
			auto drives = GetLogicalDrives();
			char volume = 'A';
			while (drives) {
				if (drives & 1) {
					paths.push_back(std::string(1, volume) + ":\\");
				}
				volume++;
				drives >>= 1;
			}

			//排除可能无法监控的盘符
			std::vector<std::string> temp;
			for (const auto& x : paths) {
				uint32_t type = GetDriveType(x.c_str());
				if (type == DRIVE_UNKNOWN ||
					type == DRIVE_NO_ROOT_DIR ||
					type == DRIVE_REMOTE ||
					type == DRIVE_CDROM) {
					continue;
				}
				temp.push_back(x);
			}

			paths = temp;

			if (path == EXCEPT_SYSTEM_DISK_PATHS) {
				char dir[512] = { 0 };
				if (!GetWindowsDirectoryA(dir, sizeof(dir))) {
					setLastError("获取系统目录失败,错误代码:%lu", GetLastError());
					break;
				}

				const std::string win(dir, 3);
				for (auto iter = paths.begin(); iter != paths.end(); ++iter) {
					if (*iter == win) {
						paths.erase(iter);
						break;
					}
				}
			}
		}
		else
		{
			std::string str = path;
			char c = str.at(str.length() - 1);
			if (c != '\\' && c != '/') {
				str.append("\\");
			}

			for (auto& x : str) {
				if (x == '/') {
					x = '\\';
				}
			}
			paths.push_back(str);
		}

		for (const auto& x : paths) {
			if (!existPath(x)) {
				Arg arg;
				if (!arg.create(x, subpath)) {
					success = false;
					setLastError(arg.error);
					if (path == ALL_DISK_PATHS || path == EXCEPT_SYSTEM_DISK_PATHS) {
						continue;
					}
					break;
				}
				m_args.push_back(arg);
			}
		}

		if (!success) {
			for (const auto& x : paths) {
				removePath(x);
			}
			break;
		}

		result = true;
	} while (false);
	return result;
}

void FileGuard::removePath(const std::string& path)
{
	for (auto iter = m_args.begin(); iter != m_args.end(); ++iter) {
		if (iter->path == path) {
			iter->release();
			m_args.erase(iter);
			break;
		}
	}
}

void FileGuard::clearPaths()
{
	for (auto iter = m_args.begin(); iter != m_args.end(); ++iter) {
		iter->release();
	}
	m_args.clear();
}

std::map<std::string, bool> FileGuard::getPaths() const
{
	std::map<std::string, bool> map;
	for (const auto& x : m_args) {
		map.insert(std::make_pair(x.path, x.subpath));
	}
	return map;
}

void FileGuard::start()
{
	if (m_suffixes.size() == 1 && m_suffixes[0] == ".*") {
		m_suffixes.clear();
	}

	for (auto& x : m_args) {
		if (!x.quit) {
			if (m_pause && onStatus) {
				onStatus(Status::STARTED, x.thread, x.path.c_str());
			}
			continue;
		}

		x.future = std::async([&](Arg* arg)->void
		{
			arg->quit = false;
			arg->thread = GetCurrentThreadId();
			if (onStatus) {
				onStatus(Status::STARTED, x.thread, x.path.c_str());
			}

			bool success = true;
			DWORD bytes = 0, offset = 0, error = 0;
			OVERLAPPED lapped = { 0 };
			arg->lapped = static_cast<void*>(&lapped);
			lapped.hEvent = arg->revent;
			do {
				memset(arg->buffer, 0, arg->size);
				print("thread %lu,path %s,start ReadDirectoryChangesW\n", arg->thread, arg->path.c_str());
				if (ReadDirectoryChangesW(arg->file,
					arg->buffer,
					arg->size,
					arg->subpath,
					FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
					&bytes,
					&lapped,
					nullptr)) {
					print("thread %lu,path %s,start GetOverlappedResult\n", arg->thread, arg->path.c_str());
					if (GetOverlappedResult(arg->file, &lapped, &bytes, TRUE)) {
						FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(arg->buffer);
						do {
							if (!m_pause && onChanged) {
								std::wstring ws(info->FileName, info->FileNameLength / sizeof(wchar_t));
								std::string s(arg->path + unicode2ansi(ws));

								if (!m_suffixes.empty()) {
									for (size_t i = 0; i < m_suffixes.size(); ++i) {
										size_t npos = s.find_last_of('.');
										if (npos != std::string::npos) {
											std::string suffix = s.substr(npos);
											std::transform(suffix.begin(), suffix.end(), suffix.begin(), std::tolower);
											if (suffix == m_suffixes[i]) {
												onChanged(info->Action, s.c_str());
												break;
											}
										}
									}
								}
								else {
									onChanged(info->Action, s.c_str());
								}
							}
							offset = info->NextEntryOffset;
							info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<uint8_t*>(info) + offset);
						} while (offset);
					}
					else {
						arg->ecode = GetLastError();
						print("thread %lu,path %s,GetOverlappedResult false,error %lu\n",
							arg->thread, arg->path.c_str(), arg->ecode);
						if (arg->ecode == ERROR_OPERATION_ABORTED) {
							arg->ecode = 0;
							break;
						}
						success = false;
					}

					print("thread %lu,path %s,start ResetEvent\n", arg->thread, arg->path.c_str());
					if (!ResetEvent(lapped.hEvent)) {
						arg->ecode = GetLastError();
						print("thread %lu,path %s,ResetEvent false,error %lu\n",
							arg->thread, arg->path.c_str(), arg->ecode);
						success = false;
						break;
					}
				}
				else {
					arg->ecode = GetLastError();
					print("thread %lu,path %s,ReadDirectoryChangeW false,error %lu\n",
						arg->thread, arg->path.c_str(), arg->ecode);
					success = false;
					break;
				}
				print("thread %lu,path %s,start WaitForSingleObject\n", arg->thread, arg->path.c_str());
				error = WaitForSingleObject(arg->wevent, 0);
			} while (error == WAIT_TIMEOUT && !arg->quit);

			if (onError && !success) {
				onError(arg->ecode, arg->path.c_str());
			}

			if (onStatus) {
				onStatus(Status::STOPPED, arg->thread, arg->path.c_str());
			}
			print("thread %lu,path %s,thread exit\n", arg->thread, arg->path.c_str());
			arg->quit = true;
		}, &x);
	}
	m_start = true;
	m_pause = false;
}

void FileGuard::pause()
{
	if (onStatus && !m_pause) {
		for (auto& x : m_args) {
			onStatus(Status::PAUSED, x.thread, x.path.c_str());
		}
	}
	m_pause = true;
	m_start = false;
}

void FileGuard::stop()
{
	m_start = false;
	m_pause = false;
	for (auto& x : m_args) {
		if (x.quit) {
			continue;
		}
		x.wait();
	}
}

bool FileGuard::restart()
{
	stop();
	std::vector<Arg> args = m_args;
	clearPaths();
	for (const auto& x : args) {
		if (!addPath(x.path, x.subpath)) {
			return false;
		}
	}
	start();
	return true;
}

bool FileGuard::isStart() const
{
	return m_start;
}

const char* FileGuard::getLastError() const
{
	return m_error.c_str();
}

void FileGuard::addSuffix(const std::string& suffix)
{
	std::string data(suffix);
	std::transform(data.begin(), data.end(), data.begin(), std::tolower);
	bool find = false, add = false;
	for (const auto& x : m_suffixes) {
		if (x == data) {
			find = true;
			break;
		}
	}

	if (!find) {
		if (data.find_last_of('.') == std::string::npos) {
			add = true;
		}
		m_suffixes.push_back(add ? "." + data : data);
	}

	for (const auto& x : m_suffixes) {
		if (x == ALL_SUFFIXES || std::string(".") + x == ALL_SUFFIXES) {
			m_suffixes.clear();
			m_suffixes.push_back(ALL_SUFFIXES);
			break;
		}
	}
}

void FileGuard::addSuffixes(const std::vector<std::string>& suffixes)
{
	m_suffixes = suffixes;
}

void FileGuard::removeSuffix(const std::string& suffix)
{
	std::string data(suffix);
	std::transform(data.begin(), data.end(), data.begin(), std::tolower);
	for (auto iter = m_suffixes.begin(); iter != m_suffixes.end(); ++iter) {
		if (*iter == data) {
			m_suffixes.erase(iter);
			break;
		}
	}
}

void FileGuard::removeSuffixes(const std::vector<std::string>& suffixes)
{
	for (auto iter = suffixes.begin(); iter != suffixes.end(); ++iter) {
		std::string data(*iter);
		std::transform(data.begin(), data.end(), data.begin(), std::tolower);
		m_suffixes.erase(std::remove(m_suffixes.begin(), m_suffixes.end(), data), m_suffixes.end());
	}
}

void FileGuard::clearSuffixes()
{
	m_suffixes.clear();
}

std::vector<std::string> FileGuard::getSuffixes() const
{
	return m_suffixes;
}

void FileGuard::setLastError(const char* fmt, ...)
{
	char buff[512] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buff, sizeof(buff), fmt, ap);
	va_end(ap);
	m_error = buff;
}

FileGuard::Arg::Arg()
	: subpath(false),
	buffer(nullptr),
	file(INVALID_HANDLE_VALUE),
	wevent(nullptr),
	revent(nullptr),
	lapped(nullptr),
	quit(true),
	thread(0),
	ecode(0),
	error{ 0 }
{
	buffer = new char[size];
	print("%s\n", __FUNCTION__);
}

FileGuard::Arg::~Arg()
{
	if (buffer) {
		delete[] buffer;
		buffer = nullptr;
	}
	print("%s\n", __FUNCTION__);
}

FileGuard::Arg::Arg(const Arg& o)
	: buffer(nullptr)
{
	path = o.path;
	subpath = o.subpath;

	buffer = new char[size];
	if (o.buffer) {
		memcpy(buffer, o.buffer, o.size);
	}
	file = o.file;
	wevent = o.wevent;
	revent = o.revent;
	lapped = o.lapped;
	quit = o.quit;
	thread = o.thread;
	ecode = o.ecode;
	memcpy(error, o.error, sizeof(error));
	print("%s\n", __FUNCTION__);
}

FileGuard::Arg& FileGuard::Arg::operator=(const Arg& o)
{
	if (this == &o) {
		return *this;
	}

	path = o.path;
	subpath = o.subpath;
	if (o.buffer) {
		memcpy(buffer, o.buffer, o.size);
	}
	file = o.file;
	wevent = o.wevent;
	revent = o.revent;
	lapped = o.lapped;
	quit = o.quit;
	thread = o.thread;
	ecode = o.ecode;
	memcpy(error, o.error, sizeof(error));
	print("%s\n", __FUNCTION__);
	return *this;
}

bool FileGuard::Arg::create(const std::string& path, bool subpath)
{
	bool result = false;
	do {
		const char c = path.at(path.length() - 1);
		this->path = (c != '\\' && c != '/') ? (path + "\\") : (path);

		for (auto& x : this->path) {
			if (x == '/')
				x = '\\';
		}

		this->subpath = subpath;

		file = CreateFileA(path.c_str(),
			GENERIC_READ | GENERIC_WRITE | FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			sprintf_s(error, "获取%s路径句柄失败,错误代码:%lu", path.c_str(), ::GetLastError());
			break;
		}

		wevent = CreateEventA(nullptr, true, false, nullptr);
		if (wevent == nullptr) {
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			sprintf_s(error, "创建%s路径事件失败,错误代码:%lu", path.c_str(), ::GetLastError());
			break;
		}

		revent = CreateEventA(nullptr, true, false, nullptr);
		if (revent == nullptr) {
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			CloseHandle(wevent);
			wevent = nullptr;
			sprintf_s(error, "创建%s路径折叠失败,错误代码:%lu", path.c_str(), ::GetLastError());
			break;
		}

		//buffer = new char[size];
		//if (buffer == nullptr) {
		//	CloseHandle(file);
		//	file = INVALID_HANDLE_VALUE;
		//	CloseHandle(wevent);
		//	wevent = nullptr;
		//	CloseHandle(revent);
		//	revent = nullptr;
		//	sprintf_s(error, "创建%s路径缓冲区失败,内存不足", path.c_str());
		//	break;
		//}
		memset(buffer, 0, size);
		result = true;
	} while (false);
	return result;
}

void FileGuard::Arg::release()
{
	if (file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(file);
		file = INVALID_HANDLE_VALUE;
	}

	if (wevent)
	{
		CloseHandle(wevent);
		wevent = nullptr;
	}

	if (revent)
	{
		CloseHandle(revent);
		revent = nullptr;
	}

	if (lapped)
	{
		lapped = nullptr;
	}

	if (buffer)
	{
		delete[] buffer;
		buffer = nullptr;
	}
}

void FileGuard::Arg::wait(size_t ms)
{
	auto ok = false;
	auto tick = GetTickCount64();
	std::future_status status = std::future_status::timeout;
	do 
	{
		if (!ok) {
			ok = CancelIoEx(file, static_cast<LPOVERLAPPED>(lapped));
		}

		status = future.wait_for(std::chrono::milliseconds(10));

		if (GetTickCount64() - tick > ms) {
			break;
		}
	} while (status != std::future_status::ready);

	if (!quit) {
		quit = true;
	}

	if (future.valid()) {
		future.get();
	}
}

#if defined(FILE_GUARD_C_API)

#define get_guard(x) ((FileGuard*)(x))

void* file_guard_new()
{
	return new FileGuard;
}

void file_guard_free(void* guard)
{
	if (guard) {
		delete static_cast<FileGuard*>(guard);
	}
}

bool file_guard_exist_path(void* guard, const char* path)
{
	return get_guard(guard)->existPath(path);
}

bool file_guard_add_path(void* guard, const char* path, bool subpath)
{
	return get_guard(guard)->addPath(path, subpath);
}

void file_guard_remove_path(void* guard, const char* path)
{
	get_guard(guard)->removePath(path);
}

void file_guard_clear_paths(void* guard)
{
	get_guard(guard)->clearPaths();
}

int file_guard_get_paths(void* guard, file_guard_path* path, int size)
{
	auto map = get_guard(guard)->getPaths();
	int index = 0;
	for (const auto& x : map)
	{
		strcpy_s(path[index].path, x.first.c_str());
		path[index].subpath = x.second;
		if (index + 1 == size)
		{
			break;
		}
		++index;
	}
	return index;
}

void file_guard_set_on_changed_callback(void* guard, void(*callback)(uint32_t action, const char* file, void* user), void* user)
{
	get_guard(guard)->onChanged = [user, callback](uint32_t action, const char* file) {
		callback(action, file, user);
	};
}

void file_guard_set_on_status_callback(void* guard, void(*callback)(int status, uint32_t thread, const char* path, void* user), void* user)
{
	get_guard(guard)->onStatus = [user, callback](int status, uint32_t thread, const char* path) {
		callback(status, thread, path, user);
	};
}

void file_guard_set_on_error_callback(void* guard, void(*callback)(uint32_t error, const char* path, void* user), void* user)
{
	get_guard(guard)->onError = [user, callback](uint32_t error, const char* path) {
		callback(error, path, user);
	};
}

void file_guard_start(void* guard)
{
	get_guard(guard)->start();
}

void file_guard_start_ex(void* guard, void* user,
	void(*on_changed)(uint32_t action, const char* file, void* user),
	void(*on_status)(int status, uint32_t thread, const char* path, void* user),
	void(*on_error)(uint32_t error, const char* path, void* user))
{
	get_guard(guard)->onChanged = [user, on_changed](uint32_t action, const char* file) {
		on_changed(action, file, user);
	};

	get_guard(guard)->onStatus = [user, on_status](int status, uint32_t thread, const char* path) {
		on_status(status, thread, path, user);
	};

	get_guard(guard)->onError = [user, on_error](uint32_t error, const char* path) {
		on_error(error, path, user);
	};

	get_guard(guard)->start();
}

void file_guard_pause(void* guard)
{
	get_guard(guard)->pause();
}

void file_guard_stop(void* guard)
{
	get_guard(guard)->stop();
}

bool file_guard_restart(void* guard)
{
	return get_guard(guard)->restart();
}

bool file_guard_is_start(void* guard)
{
	return get_guard(guard)->isStart();
}

void file_guard_get_error(void* guard, char* error, int size)
{
	strncpy_s(error, size, get_guard(guard)->getLastError(), _TRUNCATE);
}

void file_guard_add_suffix(void* guard, const char* suffix)
{
	get_guard(guard)->addSuffix(suffix);
}

void file_guard_remove_suffix(void* guard, const char* suffix)
{
	get_guard(guard)->removeSuffix(suffix);
}

void file_guard_clear_suffixes(void* guard)
{
	get_guard(guard)->clearSuffixes();
}

int file_guard_get_suffixes(void* guard, char(*suffixes)[256], int size)
{
	auto datas = get_guard(guard)->getSuffixes();
	size_t i = 0;
	for (; i < datas.size(); ++i)
	{
		strcpy_s(suffixes[i], datas[i].c_str());
		if (i == size - 1)
		{
			i++;
			break;
		}
	}
	return (int)i;
}

#endif // !FILE_GUARD_BUILD_DLL

