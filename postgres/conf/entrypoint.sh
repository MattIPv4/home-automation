#!/bin/sh

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" <<EOF
-- Create the pg_stat_statements extension
CREATE EXTENSION pg_stat_statements;
SELECT * FROM pg_extension;

-- Create the postgres_ro user
CREATE USER postgres_ro WITH PASSWORD 'postgres_ro';
GRANT pg_read_all_data TO postgres_ro;
GRANT pg_read_all_stats TO postgres_ro;
GRANT pg_monitor TO postgres_ro;
EOF
