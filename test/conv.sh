#!/bin/sh
bin2c() {
	printf "unsigned char %s[] = {\n%s\n};\n" "$1" "$(xxd -i)"
}
deflate_stream() {
	gzip --no-name | tail --bytes=+11 | head --bytes=-8
}

BASENAME=$(basename $1)
BASENAME_NO_EXT=${BASENAME%%.*}
OUT_RAW="${BASENAME_NO_EXT}_raw"
OUT_COMPRESSED="${BASENAME_NO_EXT}_compressed"

cat $1 | bin2c $OUT_RAW > ${OUT_RAW}.h
cat $1 | deflate_stream | wc -c
cat $1 | deflate_stream | bin2c $OUT_COMPRESSED > ${OUT_COMPRESSED}.h
