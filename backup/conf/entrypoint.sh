#!/bin/sh

set -e -o pipefail

# Create a fresh backup directory
directory="/opt/backup"
mkdir -p "$directory"
rm -rf "$directory"/*

# Read in all the databases available
databases=$(psql -qtAc "SELECT datname FROM pg_database WHERE datistemplate = false;")

# Loop through each database and dump it
for db in $databases; do
    echo "Dumping database: $db"
    mkdir -p "$directory/$db"

    set +e;
    (
        set -e;

        # Read in all the tables in the database
        tables=$(psql -d "$db" -qtAc "SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';")

        # Loop through each table and dump it
        for table in $tables; do
            echo "Dumping table: $db/$table"
            
            set +e;
            (
                set -e;

                # Generate the raw SQL dump for the table
                pg_dump --clean --if-exists --no-owner --no-acl --schema=public --table="$table" --format=plain --file="$directory/$db/$table.sql" "$db"

                # Compress the dumped table
                tar --create --gzip --file="$directory/$db/$table.sql.tar.gz" --directory="$directory/$db" "$table.sql"

                rm "$directory/$db/$table.sql"

                # TODO: Write the backup to a remote location
            )

            if [ "$?" != "0" ]; then
                echo "Failed to dump table: $db/$table"
            fi
            set -e;
        done
    )

    if [ "$?" != "0" ]; then
        echo "Failed to dump database: $db"
    fi
    set -e;
done

echo "Backup completed"
