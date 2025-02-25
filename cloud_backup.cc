#include "cloud_backup.hpp"

void test(const std::string filename)
{
    cloud_backup::FileUtil file(filename);
    file.Compression("test.lz");
    cloud_backup::FileUtil file2("test.lz");
    file2.UnCompression("test.txt");

    // file.Clear();
    // std::string buffer;
    // file.GetContent(&buffer);
    // cloud_backup::FileUtil file2("test.txt");
    // file2.SetContent(buffer);
}

int main(int argc, char *argv[])
{
    test(argv[1]);

    // loud_backup::FileUtil file("./makefile");
    // cloud_backup::FileUtil file("./makefile");
    // std::cout << file.GetFileName() << std::endl;
    // std::cout << file.GetFileSize() << std::endl;
    // std::cout << file.LastAccTime() << std::endl;
    // std::cout << file.LastModTime() << std::endl;
    return 0;
}