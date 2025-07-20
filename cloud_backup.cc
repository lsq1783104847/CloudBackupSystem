#include "cloud_backup.hpp"
#include "llhttp.h"

void ConfigTest()
{
    auto p = cloud_backup::Config::GetInstance();
    std::cout << p->GetHotTime() << std::endl;
    std::cout << p->GetServerIp() << std::endl;
    std::cout << p->GetServerPort() << std::endl;
    std::cout << p->GetCompressionFileSuffix() << std::endl;
}

void JsonTest()
{
    const char *name = "小明";
    Json::Value root;
    root["姓名"] = name;
    root["年龄"] = 11;
    root["成绩"].append(" 尕娃娃5");
    root["成绩"].append("AW嘎我给");
    root["成绩"].append("官网网");

    std::string str;
    cloud_backup::JsonUtil::Serialize(root, &str);

    std::cout << str << std::endl;

    Json::Value tmp;
    cloud_backup::JsonUtil::Deserialize(str, &tmp);

    std::cout << tmp["姓名"].asString() << std::endl;
    std::cout << tmp["年龄"].asInt() << std::endl;
    for (int i = 0; i < tmp["成绩"].size(); i++)
        std::cout << tmp["成绩"][i].asString() << std::endl;
}

void CompressionTest(const std::string filename)
{
    cloud_backup::FileUtil file(filename);
    file.Compression("test.lz");
    cloud_backup::FileUtil file2("test.lz");
    file2.UnCompression("test.txt");
}

void FileUtilTest1()
{
    cloud_backup::FileUtil file("./makefile");
    std::cout << file.GetFilePath() << std::endl;
    std::cout << file.GetFileName() << std::endl;
    std::cout << file.GetFileSize() << std::endl;
    std::cout << file.LastAccTime() << std::endl;
    std::cout << file.LastModTime() << std::endl;
}

void FileUtilTest2()
{
    cloud_backup::FileUtil directy("./a/b");
    std::cout << directy.Exists() << std::endl;
    directy.CreateDirectories();
    std::cout << directy.Exists() << std::endl;
    std::vector<cloud_backup::FileUtil> files;
    directy.ScanDirectory(&files);
    for (auto &file : files)
        std::cout << file.GetFilePath() << std::endl;
}

void DateManagerTest()
{
    cloud_backup::DataManager::ptr dm = cloud_backup::DataManager::GetInstance();
    // cloud_backup::BackupInfo::ptr info1 = cloud_backup::BackupInfo::NewBackupInfo("./makefile");
    // cloud_backup::BackupInfo::ptr info2 = cloud_backup::BackupInfo::NewBackupInfo("./bundle.h");
    // cloud_backup::BackupInfo::ptr info3 = cloud_backup::BackupInfo::NewBackupInfo("./httplib.h");
    // if (info1)
    //     dm->Insert(*info1);
    // if (info2)
    //     dm->Insert(*info2);
    // if (info3)
    //     dm->Insert(*info3);
    std::vector<cloud_backup::BackupInfo> infos;
    dm->GetAll(&infos);
    for (auto &info : infos)
    {
        std::cout << "File: " << info._filename << std::endl;
        std::cout << "Size: " << info._fsize << std::endl;
        std::cout << "Last Access Time: " << info._atime << std::endl;
        std::cout << "Last Modify Time: " << info._mtime << std::endl;
        std::cout << "Compressed: " << (info._compress_flag ? "Yes" : "No") << std::endl;
    }
    // dm->Delete("makefile");
    // dm->GetAll(&infos);
    // std::cout << "After deletion:-----------------------------------" << std::endl;
    // for (auto &info : infos)
    // {
    //     std::cout << "File: " << info._filename << std::endl;
    //     std::cout << "Size: " << info._fsize << std::endl;
    //     std::cout << "Last Access Time: " << info._atime << std::endl;
    //     std::cout << "Last Modify Time: " << info._mtime << std::endl;
    //     std::cout << "Compressed: " << (info._compress_flag ? "Yes" : "No") << std::endl;
    // }
    cloud_backup::BackupInfo::ptr pinfo = dm->GetOneByFileName("bundle.h");
    if (pinfo != nullptr)
    {
        std::cout << "Select File: " << pinfo->_filename << std::endl;
        std::cout << "Size: " << pinfo->_fsize << std::endl;
        std::cout << "Last Access Time: " << pinfo->_atime << std::endl;
        std::cout << "Last Modify Time: " << pinfo->_mtime << std::endl;
        std::cout << "Compressed: " << (pinfo->_compress_flag ? "Yes" : "No") << std::endl;
    }
    else
    {
        std::cout << "File not found." << std::endl;
    }
}

// int main(int argc, char *argv[])
// {
//     cloud_backup::InitCloudBackupLogger();

//     // ConfigTest();

//     // JsonTest();

//     // CompressionTest(argv[1]);

//     // FileUtilTest1();

//     // FileUtilTest2();

//     DateManagerTest();

//     return 0;
// }

int handle_on_message_complete(llhttp_t* parser) {
	fprintf(stdout, "Message completed!\n");
	return 0;
}

int main() {
	llhttp_t parser;
	llhttp_settings_t settings;

	/*Initialize user callbacks and settings */
	llhttp_settings_init(&settings);

	/*Set user callback */
	settings.on_message_complete = handle_on_message_complete;

	/*Initialize the parser in HTTP_BOTH mode, meaning that it will select between
	*HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
	*input.
	*/
	llhttp_init(&parser, HTTP_BOTH, &settings);

	/*Parse request! */
	const char* request = "GET / HTTP/1.1\r\n\r\n";
	int request_len = strlen(request);

	enum llhttp_errno err = llhttp_execute(&parser, request, request_len);
	if (err == HPE_OK) {
		fprintf(stdout, "Successfully parsed!\n");
	} else {
		fprintf(stderr, "Parse error: %s %s\n", llhttp_errno_name(err), llhttp_get_error_reason(&parser));
	}
}