# c-async-db-api
C API using select for async socket connections. 

Uses a separate database API that uses bin files to store data.

This branch uses select for both recv and send, so both are async

In conclusion, i won't merge it, because it made little to no impact on performance, and it's a lot more complex than the previous version.