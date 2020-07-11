/*
 * Copyright (C) 2020 Thomas Folkert Mol
 * This work is free to be used, distributed, and modified under the terms of
 * EUPL-1.2-or-later. If you did not receive a copy of this license with this
 * source code, you can find it at
 * https://joinup.ec.europa.eu/collection/eupl/eupl-text-eupl-12
 * in your language of choice.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define EXECUTABLE "mytar"

#define TAR_MAGIC "ustar" /* With null. */
#define TAR_VERSION "00" /* No null. */
#define OLD_MAGIC "ustar  " /* With null. */
#define VERSION_LENGTH 2
#define BLOCK_SIZE 512

#define UNKNOWN_OPTION "%s: Unknown option: %s\n"
#define MISSING_OPTIONS "%s: Need at least one option\n"
#define INCOMPATIBLE_ARGUMENTS "%s: -x and -t options are incompatible, pick one.\n"
#define LONE_ZERO_BLOCK "%s: A lone zero block at %d\n"
#define FILE_NOT_FOUND "%s: %s: Not found in archive\n"
#define NOT_FOUND_FINAL "%s: Exiting with failure status due to previous errors\n"
#define UNSUPPORTED_TYPE "%s: Unsupported header type: %d\n"
#define ARCHIVE_NOT_FOUND "%s: %s: Cannot open: No such file or directory\n%s: Error is not recoverable: exiting now\n"
#define UNEXPECTED_EOF "%s: Unexpected EOF in archive\n%s: Error is not recoverable: exiting now\n"
#define IO_ERROR "%s: I/O error upon closing file %s, written data may be lost.\n"

enum { UNEXPECTED_EOF_CODE = 1, UNSUPPORTED_TYPE_CODE = 2 };


struct header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char link_name[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char dev_major[8];
	char dev_minor[8];
	char prefix[155];
	/* Pad 12 bytes for a clean 512 total, eases reading into buffer and
	 * checking for zero blocks.
	 */
	char pad[12];
};


int check_eof(FILE *fp)
{
	int c = fgetc(fp);
	if (c == EOF)
		return 1;
	fseek(fp, -1, SEEK_CUR);
	fputc(c, fp);
	return 0;
}


int zero_block(void *memory)
{	
	/* Note that this function expects a memory block of BLOCK_SIZE bytes,
	 * feeding it a smaller block can lead to undefined behaviour or a
	 * segmentation fault. The caller must verify that it has a fully sized
	 * block.
	 */
	size_t i = 0;
	
	while (i < BLOCK_SIZE)
		if (((char*) memory)[i++])
			return 0;

	return 1;
}


int last_block(FILE *fp, char *prog, int blocks)
{
	char buffer[BLOCK_SIZE];
	if (check_eof(fp))
		fprintf(stderr, LONE_ZERO_BLOCK, prog, blocks + 1);
	else if (!fread(buffer, BLOCK_SIZE, 1, fp))
		return UNEXPECTED_EOF_CODE;
	else if (!zero_block((void*) &buffer))
		fprintf(stderr, LONE_ZERO_BLOCK, prog, blocks + 1);
	
	return 0;
}


int block_align(FILE *fp)
{
	return fseek(fp, (BLOCK_SIZE - ftell(fp) % BLOCK_SIZE) % BLOCK_SIZE, 
			SEEK_CUR);
}


int select(char *name, int select_count, char *select_files[])
{
	for (int i = 0; i < select_count; ++i) {
		if (!strcmp(name, select_files[i])) {
			select_files[i] = "";
			return 1;
		}
	}
	return 0;
}

void extract_file(FILE *fp, struct header head)
{
}

int read_headers(FILE *fp, char *prog, int select_count, char *select_files[],
		int verbose, int extract)
{
	struct header head;
	long file_size;
	int blocks;

	for (;;) {
		blocks = ftell(fp) / BLOCK_SIZE;

		if (check_eof(fp)) {
			if (fseek(fp, -1, SEEK_CUR) || check_eof(fp))
				return UNEXPECTED_EOF_CODE;
			return 0;
		}

		if (!fread(&head, BLOCK_SIZE, 1, fp))
			return UNEXPECTED_EOF_CODE;

		if (zero_block((void*) &head))
			return last_block(fp, prog, blocks);

		if (head.typeflag && head.typeflag != '0') {
			fprintf(stderr, UNSUPPORTED_TYPE, prog, head.typeflag);
			return UNSUPPORTED_TYPE_CODE;
		}
		
		if (!select_count ||
				select(head.name, select_count, select_files)) {
			if (verbose) {
				printf("%s\n", head.name);
				fflush(stdout);
			}
			if (extract)
				extract_file(fp, head);
		}

		sscanf(head.size, "%lo", &file_size);
		fseek(fp, file_size, SEEK_CUR);
		block_align(fp);
	}
}


int main(int argc, char *argv[])
{
	char *prog = EXECUTABLE;
	char *file;
	int extract = 0;
	int truncate = 0;
	int verbose = 0;
	int select_count = 0;
	char **select_files = NULL;

	if (argc < 2) {
		fprintf(stderr, MISSING_OPTIONS, prog);
		return 2;
	}

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-f")) {
			file = argv[++i];
		} else if (!strcmp(argv[i], "-t")) {
			truncate = 1;
			verbose = 1;
		} else if (!strcmp(argv[i], "-x")) {
			extract = 1;
		} else if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if ((extract || truncate) && argv[i][0] != '-') {
			select_count = argc - i;
			select_files = &argv[i];
			break;
		} else {
			fprintf(stderr, UNKNOWN_OPTION, prog, argv[i]);
			return 2;
		}
	}
	if (extract && truncate) {
		fprintf(stderr, INCOMPATIBLE_ARGUMENTS, prog);
		return 2;
	}

	FILE *fp = fopen(file, "rb");
	if (!fp) {
		fprintf(stderr, ARCHIVE_NOT_FOUND, prog, file, prog);
		return 2;
	}
	int exit_code = read_headers(fp, prog, select_count, select_files,
			verbose, extract);
	if (exit_code == UNEXPECTED_EOF_CODE) {
		fprintf(stderr, UNEXPECTED_EOF, prog, prog);
		exit_code = 2;
	}

	for (int i = 0; i < select_count; ++i) {
		if (*select_files[i]) {
			exit_code = 3;
			fprintf(stderr, FILE_NOT_FOUND, prog, select_files[i]);
		}
	}
	if (exit_code == 3) {
		fprintf(stderr, NOT_FOUND_FINAL, prog);
		exit_code = 2;
	}

	if (!fclose(fp))		
		return exit_code;
	fprintf(stderr, IO_ERROR, prog, file);
	return 2;
}
