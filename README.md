# FileGuard
windows os file monitoring

#include "FileGuard.h"
#include <stdio.h>
int main()
{
	FileGuard guard;
	guard.addPath("*"); //*代表监控当前计算机中,所有的磁盘
	guard.onChanged = [](uint32_t action, const char* file) {
		switch (action)
		{
			case FileGuard::Action::ADDED:
				printf("onChanged-> 文件[%s]添加动作\n", file);
				break;
			case FileGuard::Action::MODIFIED:
				printf("onChanged-> 文件[%s]修改动作\n", file);
				break;;
			case FileGuard::Action::REMOVED:
				printf("onChanged-> 文件[%s]移除动作\n", file);
				break;
			case FileGuard::Action::RENAMED_OLD_NAME:
				printf("onChanged-> 文件[%s]改名动作(旧)\n", file);
				break;
			case FileGuard::Action::RENAMED_NEW_NAME:
				printf("onChanged-> 文件[%s]改名动作(新)\n", file);
				break;
		default:
			break;
		}
	};
 
	guard.onError = [](uint32_t error, const char* path) {
		printf("onError-> 错误代码:%lu,路径:%s\n", error, path);
	};
 
	guard.onStatus = [](int status, uint32_t thread, const char* path) {
		printf("onStatus-> 状态:%d,线程:%lu,路径:%s\n", status, thread, path);
	};
 
	guard.start();
	getchar();
	guard.stop();
 
	return 0;
}
