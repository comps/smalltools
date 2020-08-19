all: jitter-seeder rdrand-seeder

jitter-seeder: jitter-seeder.c
	$(CC) -Wall -Wextra -o $@ $< -lkcapi

rdrand-seeder: rdrand-seeder.c
	gcc -Wall -Wextra -mrdrnd -o $@ $<

.PHONY: clean
clean:
	rm -f jitter-seeder rdrand-seeder
