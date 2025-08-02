.PHONY:cloud_backup_server
cloud_backup_server:cloud_backup_server.cc
	g++ -o $@ $^ -std=c++20 -lpthread -ljsoncpp -lllhttp -L ./lib -I ./include

.PHONY:clean
clean:
	rm cloud_backup_server