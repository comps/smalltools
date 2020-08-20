all: jitter-seeder rdseed-seeder fake-seeder

jitter-seeder: jitter-seeder.c
	$(CC) -Wall -Wextra -o $@ $< -lkcapi

rdseed-seeder: rdseed-seeder.c
	gcc -Wall -Wextra -mrdseed -o $@ $<

fake-seeder: fake-seeder.c
	$(CC) -Wall -Wextra -o $@ $<

.PHONY: clean
clean:
	rm -f jitter-seeder rdseed-seeder fake-seeder
