#include "cloud_backup.hpp"

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

int main(int argc, char *argv[])
{
    cloud_backup::InitCloudBackupLogger();

    // ConfigTest();

    // JsonTest();

    // CompressionTest(argv[1]);

    // FileUtilTest1();

    FileUtilTest2();

    return 0;
}