#include "cloud_backup_server.hpp"

void ConfigTest()
{
    auto p = cloud_backup::Config::GetInstance();
    std::cout << p->GetServerPort() << std::endl;
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

void FileUtilTest1()
{
    cloud_backup::FileUtil file("./makefile");
    std::cout << file.GetFilePath() << std::endl;
    std::cout << file.GetFileName() << std::endl;
    std::cout << file.GetFileSize() << std::endl;
}

void FileUtilTest2()
{
    cloud_backup::FileUtil file("./test.txt");
    // std::string buffer;
    // file.GetContent(&buffer);
    // std::cout << buffer << std::endl;

    // file.AppendContent("hello world");

    // file.Clear();
}

void FileUtilTest3()
{
    // cloud_backup::FileUtil directy("./a/b");
    // directy.CreateDirectories();

    std::vector<cloud_backup::FileUtil> files;
    cloud_backup::FileUtil newfile("./a/b/test");
    newfile.ScanDirectory(&files);
    for (auto &file : files)
        std::cout << file.GetFilePath() << std::endl;
    newfile.RemoveRegularFile();
}

void DateManagerTest()
{
    cloud_backup::DataManager::ptr dmp = cloud_backup::DataManager::GetInstance();
    dmp->Register("test1");
    dmp->Register("test2");
    dmp->Register("test3");
    dmp->Register("test4");
    dmp->Deregister("test4");
    dmp->Insert("test1", 1029);
    dmp->Insert("test2", 10290);
    dmp->Insert("test3", 1028);
    auto p = dmp->GetFileInfoNode("test1");
    auto s = dmp->GetFileSize("test2");
    std::cout << p->_info._filename << "——" << s << "——" << p->_info._time << "\n";
    std::vector<cloud_backup::BackupInfoNode> infos;
    dmp->GetAllBackupInfo(&infos);
    for (auto &e : infos)
        std::cout << e._filename << "——" << e._size << "——" << e._time << "\n";
    std::cout << dmp->GetFilePreContent("test1") << "\n";
    dmp->PutFilePreContent("test1", "hello world");
    std::cout << dmp->GetFilePreContent("test1") << "\n";
    dmp->PutFilePreContent("test2", "thank you");
    std::cout << dmp->GetFilePreContent("test1") << "\n";
    std::cout << dmp->GetFilePreContent("test2") << "\n";
    dmp->PutFilePreContent("test3", "you are welcome");
    std::cout << dmp->GetFilePreContent("test1") << "\n";
    std::cout << dmp->GetFilePreContent("test2") << "\n";
    std::cout << dmp->GetFilePreContent("test3") << "\n";

    dmp->Delete("test1");
}

int main(int argc, char *argv[])
{
    // 初始化日志器
    cloud_backup::InitCloudBackupLogger();

    cloud_backup::CloudBackupServer cloud_backup_server(argv[0]);

    return 0;
}