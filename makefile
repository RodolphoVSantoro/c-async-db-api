server-main=src/api.c
server-output=server
server-release_output=rinha-backend-2024
db-main=src/db.c
db-output=db
db-release_output=rinha-db-2024
compiler=gcc
warn=-Wall -Wextra -Werror -pedantic
flags=-std=gnu99
debug=-fsanitize=address -g
release=-O3

ifndef PORT
override PORT = 9999
endif

ifndef DB_PORT
override DB_PORT = 5000
endif

build-server:
	$(compiler) -o $(server-output) $(flags) $(debug) $(warn) $(server-main)

build-server-release: $(server-main) $(server-headers)
	$(compiler) -o $(server-release_output) $(flags) $(warn) $(release) $(server-main)

server-run:
	./$(server-output) $(PORT) $(DB_PORT)

server-run-release:
	./$(server-release_output) $(PORT) $(DB_PORT)


build-db:
	$(compiler) -o $(db-output) $(flags) $(debug) $(warn) $(db-main)

build-db-release: $(db-main) $(db-headers)
	$(compiler) -o $(db-release_output) $(flags) $(warn) $(release) $(db-main)

db-run:
	./$(db-output) $(DB_PORT)

db-run-release:
	./$(db-release_output) $(DB_PORT)
