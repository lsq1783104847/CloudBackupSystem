.PHONY:cloud_backup
cloud_backup:cloud_backup.cc
	g++ -o $@ $^ -std=c++20 -lpthread -ljsoncpp -lllhttp -L ./lib -I ./include -g

.PHONY:clean
clean:
	rm cloud_backup