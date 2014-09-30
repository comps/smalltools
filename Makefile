all: ethtoolspeed.so

ethtoolspeed.so: ethtoolspeed.c
	$(CC) -fPIC -shared -ldl -Wall -Wextra $< -o $@
	chmod +x $@

.PHONY: clean
clean:
	rm -f ethtoolspeed.so
