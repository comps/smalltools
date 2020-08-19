all: jitter-seeder rdseed-seeder

jitter-seeder: jitter-seeder.c
	$(CC) -Wall -Wextra -o $@ $< -lkcapi

rdseed-seeder: rdseed-seeder.c
	gcc -Wall -Wextra -mrdseed -o $@ $<

.PHONY: clean
clean:
	rm -f jitter-seeder rdseed-seeder
