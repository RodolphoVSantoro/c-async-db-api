FROM ubuntu:23.10 as builder
RUN apt-get update && apt-get install -y gcc build-essential

COPY ./src /app/src
COPY ./server/makefile /app/server/makefile
COPY ./db/makefile /app/db/makefile

WORKDIR /app/server
RUN make build-release

WORKDIR /app/db
RUN make build-release

FROM ubuntu:23.10 as final

COPY --from=builder /app/server/rinha-backend-2024 /app/rinha-backend-2024
COPY --from=builder /app/db/rinha-db-2024 /app/rinha-db-2024

WORKDIR /app
