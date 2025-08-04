#ifndef __FILE_GUARD_H__
#define __FILE_GUARD_H__

#include <functional>
#include <algorithm>
#include <future>
#include <string>
#include <vector>
#include <map>

class FileGuard
{
public:
	// 文件动作
	enum Action
	{
		// 添加动作
		ADDED = 0x00000001,

		// 删除动作
		REMOVED,

		// 修改动作
		MODIFIED,

		// 重命名旧名称动作
		RENAMED_OLD_NAME,

		// 重命名新名称动作
		RENAMED_NEW_NAME,
	};

	// 监控状态
	enum Status {
		// 已开始
		STARTED,

		// 已暂停
		PAUSED,

		// 已停止
		STOPPED,
	};

	/*
	* @brief 构造
	*/
	FileGuard();

	/*
	* @brief 析构
	*/
	~FileGuard();

	/*
	* @brief 存在路径
	* @param[in] path 路径
	* @retval true 存在
	* @retval false 不存在
	*/
	bool existPath(const std::string& path) const;

	/*
	* @brief 添加路径
	* @param[in] path 路径(*代表监控所有磁盘)(&代表监控除系统盘以外的磁盘)
	* @param[in] subpath 是否监控子路径
	* @retval true 成功
	* @retval false 失败
	*/
	bool addPath(const std::string& path, bool subpath = true);

	/*
	* @brief 删除路径
	* @param[in] path 路径
	* @return void
	*/
	void removePath(const std::string& path);

	/*
	* @brief 清空路径
	* @return void
	*/
	void clearPaths();

	/*
	* @brief 获取路径
	* @return (路径,是否监控子路径)
	*/
	std::map<std::string, bool> getPaths() const;

	/*
	* @brief 启动
	* @return void
	*/
	void start();

	/*
	* @brief 暂停
	* @return void
	*/
	void pause();

	/*
	* @brief 停止
	* @return void
	*/
	void stop();

	/*
	* @brief 重新开始监控
	* @retval true 成功
	* @retval false 失败
	*/
	bool restart();

	/*
	* @brief 是否启动
	* @retval true 已启动
	* @retval false 未启动
	*/
	bool isStart() const;

	/*
	* @brief 获取最终错误
	* @return 最终错误
	*/
	const char* getLastError() const;

	/*
	* @brief 添加后缀
	* @param[in] suffix 后缀名(.*或*代表所有)
	* @return void
	*/
	void addSuffix(const std::string& suffix);

	/*
	* @brief 添加后缀
	* @param[in] suffixes 后缀名
	* @return void
	*/
	void addSuffixes(const std::vector<std::string>& suffixes);

	/*
	* @brief 删除后缀
	* @param[in] suffix 后缀名
	* @return void
	*/
	void removeSuffix(const std::string& suffix);

	/*
	* @brief 删除后缀
	* @param[in] suffixes 后缀名
	* @return void
	*/
	void removeSuffixes(const std::vector<std::string>& suffixes);

	/*
	* @brief 清空后缀
	* @return void
	*/
	void clearSuffixes();

	/*
	* @brief 获取后缀
	* @return void
	*/
	std::vector<std::string> getSuffixes() const;

	//改变回调
	std::function<void(uint32_t action, const char* file)> onChanged = nullptr;

	//错误回调
	std::function<void(uint32_t error, const char* path)> onError = nullptr;

	//状态回调
	std::function<void(int status, uint32_t thread, const char* path)> onStatus = nullptr;

	//所有磁盘的路径
	static const char* const ALL_DISK_PATHS;

	//除系统以外的磁盘路径
	static const char* const EXCEPT_SYSTEM_DISK_PATHS;

	//所有后缀名
	static const char* const ALL_SUFFIXES;
protected:
	/*
	* @brief 设置最终错误
	* @param[in] fmt 格式化字符串
	* @param[in] ... 可变参数
	* @return void
	*/
	void setLastError(const char* fmt, ...);

private:
	
	//参数
	struct Arg
	{
		std::string path;
		std::future<void> future;
		bool subpath;
		char* buffer;
		static const size_t size = 64 * 1024; //64kb
		void* file;
		void* wevent;
		void* revent;
		void* lapped;
		bool quit;
		unsigned long thread;
		unsigned long ecode;
		char error[256];

		Arg();

		~Arg();

		Arg(const Arg& o);

		Arg& operator=(const Arg& o);

		//创建
		bool create(const std::string& path, bool subpath);

		//释放
		void release();

		//等待
		void wait(size_t ms = 5000);
	};

	//参数
	std::vector<Arg> m_args;

	//后缀
	std::vector<std::string> m_suffixes;

	//错误信息
	std::string m_error = "未知错误";

	//是否启动
	bool m_start = false;

	//是否暂停
	bool m_pause = false;
};

#define FILE_GUARD_C_API
#if defined(FILE_GUARD_C_API)

//#define FILE_GUARD_BUILD_DLL
#if defined(FILE_GUARD_BUILD_DLL)
#define FILE_GUARD_DLL_EXPORT __declspec(dllexport)
#else
#define FILE_GUARD_DLL_EXPORT
#endif

#include <stdbool.h>
#include <stdint.h>

enum file_guard_action
{
	//添加动作
	added_action = 0x00000001,

	//删除动作
	removed_action,

	//修改动作
	modified_action,

	//重命名旧名称动作
	renamed_old_name_action,

	//重命名新名称动作
	renamed_new_name_action
};

struct file_guard_path
{
	char path[512];
	bool subpath;
};

#if defined(__cplusplus)
extern "C" {
#endif // !__cplusplus

	FILE_GUARD_DLL_EXPORT void* file_guard_new();

	FILE_GUARD_DLL_EXPORT void file_guard_free(void* guard);

	FILE_GUARD_DLL_EXPORT bool file_guard_exist_path(void* guard, const char* path);

	FILE_GUARD_DLL_EXPORT bool file_guard_add_path(void* guard, const char* path, bool subpath);

	FILE_GUARD_DLL_EXPORT void file_guard_remove_path(void* guard, const char* path);

	FILE_GUARD_DLL_EXPORT void file_guard_clear_paths(void* guard);

	FILE_GUARD_DLL_EXPORT int file_guard_get_paths(void* guard, struct file_guard_path* path, int size);

	FILE_GUARD_DLL_EXPORT void file_guard_set_on_changed_callback(void* guard,
		void (*callback)(uint32_t action, const char* file, void* user), void* user);

	FILE_GUARD_DLL_EXPORT void file_guard_set_on_status_callback(void* guard,
		void (*callback)(int status, uint32_t thread, const char* path, void* user), void* user);

	FILE_GUARD_DLL_EXPORT void file_guard_set_on_error_callback(void* guard,
		void (*callback)(uint32_t error, const char* path, void* user), void* user);

	FILE_GUARD_DLL_EXPORT void file_guard_start(void* guard);

	FILE_GUARD_DLL_EXPORT void file_guard_start_ex(void* guard, void* user,
		void (*on_changed)(uint32_t action, const char* file, void* user),
		void (*on_status)(int status, uint32_t thread, const char* path, void* user),
		void (*on_error)(uint32_t error, const char* path, void* user));

	FILE_GUARD_DLL_EXPORT void file_guard_pause(void* guard);

	FILE_GUARD_DLL_EXPORT void file_guard_stop(void* guard);

	FILE_GUARD_DLL_EXPORT bool file_guard_restart(void* guard);

	FILE_GUARD_DLL_EXPORT bool file_guard_is_start(void* guard);

	FILE_GUARD_DLL_EXPORT void file_guard_get_error(void* guard, char* error, int size);

	FILE_GUARD_DLL_EXPORT void file_guard_add_suffix(void* guard, const char* suffix);

	FILE_GUARD_DLL_EXPORT void file_guard_remove_suffix(void* guard, const char* suffix);

	FILE_GUARD_DLL_EXPORT void file_guard_clear_suffixes(void* gurad);

	FILE_GUARD_DLL_EXPORT int file_guard_get_suffixes(void* guard, char (*suffixes)[256], int size);

#if defined(__cplusplus)
}
#endif // !__cplusplus
#endif // !FILE_GUARD_C_API
#endif // !__FILE_GUARD_H__
