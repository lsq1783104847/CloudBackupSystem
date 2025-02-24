cloud_backup:cloud_backup.cc 
	g++ -o $@ $^ -std=c++11 -lpthread

.PHONY:clean
clean:
	rm cloud_backup