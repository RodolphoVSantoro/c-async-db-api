main=../src/api.c
output=server
release_output=rinha-backend-2024
compiler=gcc
warn=-Wall -Wextra -Werror -pedantic
flags=-std=gnu99
debug=-fsanitize=address -g
release=-O3
profiling=-pg

ifndef PORT
override PORT = 9999
endif

ifndef DB_PORT
override DB_PORT = 5000
endif

build:
	$(compiler) -o $(output) $(flags) $(debug) $(warn) $(main)

build-release: $(main) $(headers)
	$(compiler) -o $(release_output) $(flags) $(warn) $(release) $(main)

run:
	./$(output) $(PORT) $(DB_PORT)

run-release:
	./$(release_output) $(PORT) $(DB_PORT)

profile:
	$(compiler) -o $(output) $(flags) $(profiling) $(warn) $(main)
	./$(output) $(PORT) $(DB_PORT)

profile-image:
	gprof $(output) | gprof2dot | dot -Tpng -o profiling/output.png
