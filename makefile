cloud_backup:cloud_backup.cc
	g++ -o $@ $^ -std=c++20 -lpthread -ljsoncpp -lbundle -lllhttp -L ./lib 

.PHONY:clean
clean:
	rm cloud_backup