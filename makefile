cloud_backup:cloud_backup.cc
	g++ -o $@ $^ -std=c++17 -lpthread -ljsoncpp -lbundle -L ~/cloud_backup/lib 

.PHONY:clean
clean:
	rm cloud_backup