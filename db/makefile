main=../src/db.c
output=db
release_output=rinha-db-2024
compiler=gcc
warn=-Wall -Wextra -Werror -pedantic
flags=-std=gnu99
libs=-pthread
debug=-fsanitize=address -g
release=-O3
profiling=-pg

ifndef DB_PORT
override DB_PORT = 5000
endif

build:
	$(compiler) -o $(output) $(flags) $(libs) $(debug) $(warn) $(main)

build-release: $(main) $(headers)
	$(compiler) -o $(release_output) $(flags) $(libs) $(warn) $(release) $(main)

run:
	./$(output) $(DB_PORT)

run-release:
	./$(release_output) $(DB_PORT)

profile:
	$(compiler) -o $(output) $(flags) $(libs) $(profiling) $(warn) $(main)
	./$(output) $(PORT) $(DB_PORT)

profile-image:
	gprof $(output) | gprof2dot | dot -Tpng -o profiling/output.png
