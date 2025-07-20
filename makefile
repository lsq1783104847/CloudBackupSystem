cloud_backup:cloud_backup.cc
	g++ -o $@ $^ -std=c++20 -lpthread -ljsoncpp -lllhttp -L ./lib -I ./include

.PHONY:clean
clean:
	rm cloud_backup