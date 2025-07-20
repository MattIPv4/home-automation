#!/bin/sh

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" <<EOF
CREATE EXTENSION pg_stat_statements;
SELECT * FROM pg_extension;
EOF
