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
#define INVALID_ARCHIVE "%s: This does not look like a tar archive\n%s: Exiting with failure status due to previous errors\n"
#define LONE_ZERO_BLOCK "%s: A lone zero block at %d\n"
#define FILE_NOT_FOUND "%s: %s: Not found in archive\n"
#define NOT_FOUND_FINAL "%s: Exiting with failure status due to previous errors\n"
#define UNSUPPORTED_TYPE "%s: Unsupported header type: %d\n"
#define ARCHIVE_NOT_FOUND "%s: %s: Cannot open: No such file or directory\n%s: Error is not recoverable: exiting now\n"
#define UNEXPECTED_EOF "%s: Unexpected EOF in archive\n%s: Error is not recoverable: exiting now\n"
#define IO_ERROR "%s: I/O error with file %s, written data may be lost.\n"


enum 
{
	NO_ERROR_CODE = 0,
	INVALID_ARCHIVE_CODE,
	UNEXPECTED_EOF_CODE,
	IO_ERROR_CODE, /* For extracted files. */
	TL_IO_ERROR_CODE, /* For the input tarball. */
	UNSUPPORTED_TYPE_CODE
};


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
	/* 
	 * Pad 12 bytes for a clean 512 total, eases reading into buffer and
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
	/* 
	 * Note that this function expects a memory block of BLOCK_SIZE bytes,
	 * feeding it a smaller block can lead to undefined behaviour or a
	 * segmentation fault. The caller must verify that it has a full sized
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


int extract_file(FILE *fp, struct header head, char *prog)
{
	FILE *extract_fp = fopen(head.name, "wb");
	if (!extract_fp) {
		return IO_ERROR_CODE;
	}
	
	int return_code;

	long remaining_bytes;
	sscanf(head.size, "%lo", &remaining_bytes);
	
	long read_size = BLOCK_SIZE;
	char buffer[BLOCK_SIZE];
	while (remaining_bytes) {
		if (remaining_bytes < read_size) 
			read_size = remaining_bytes;

		if (!fread(buffer, read_size, 1, fp)) {
			if (feof(fp)) {
				return_code = UNEXPECTED_EOF_CODE;
				break;
			}
			return_code = TL_IO_ERROR_CODE;
			break;
		}

		if (!fwrite(buffer, read_size, 1, extract_fp)) {
			return_code = IO_ERROR_CODE;
			break;
		}

		remaining_bytes -= read_size;
	}

	if (fclose(extract_fp) || return_code == IO_ERROR_CODE) {
		fprintf(stderr, IO_ERROR, prog, head.name);
	}
	return return_code;
}

int check_archive(FILE *fp)
{
	struct header head;

	if (!fread(&head, BLOCK_SIZE, 1, fp)) {
		if (feof(fp))
			return INVALID_ARCHIVE_CODE;
		return TL_IO_ERROR_CODE;
	}
	
	/* The conditions below are mutually exclusive, so using AND is fine. */
	if (strcmp(head.magic, TAR_MAGIC) && strcmp(head.magic, OLD_MAGIC))
		return INVALID_ARCHIVE_CODE;

	return 0;
}


int read_headers(FILE *fp, char *prog, int select_count, char *select_files[],
		int verbose, int extract)
{
	struct header head;
	long file_size;
	int blocks;

	if (fseek(fp, 0, SEEK_SET))
		return TL_IO_ERROR_CODE;

	for (;;) {
		blocks = ftell(fp) / BLOCK_SIZE;

		if (check_eof(fp)) {
			if (fseek(fp, -1, SEEK_CUR))
				return TL_IO_ERROR_CODE;
			if (check_eof(fp))
				return UNEXPECTED_EOF_CODE;
			return 0;
		}

		if (!fread(&head, BLOCK_SIZE, 1, fp)) {
			if (feof(fp))
				return UNEXPECTED_EOF_CODE;
			return TL_IO_ERROR_CODE;
		}

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

			if (extract) {
				int extract_err = extract_file(fp, head, prog);
				if (extract_err)
					return extract_err;

				if (block_align(fp)) {
					if (feof(fp))
						return UNEXPECTED_EOF_CODE;
					return TL_IO_ERROR_CODE;
				}	
				continue;
			}
		}

		sscanf(head.size, "%lo", &file_size);
		if (fseek(fp, file_size, SEEK_CUR) || block_align(fp)) {
			return TL_IO_ERROR_CODE;
		}
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
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'f':
				file = argv[++i];
				break;
			case 't':
				truncate = 1;
				/* fall through */
			case 'v':
				verbose = 1;
				break;
			case 'x':
				extract = 1;
				break;
			default:
				fprintf(stderr, UNKNOWN_OPTION, prog, argv[i]);
				return 2;
			}
		} else if (truncate || extract) {
			select_count = argc - i;
			select_files = &argv[i];
			break;
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
	
	int exit_code;
	if (!(exit_code = check_archive(fp)))
		exit_code = read_headers(fp, prog, select_count, select_files,
				verbose, extract);

	switch (exit_code) {
	case 0:
		break;
	case INVALID_ARCHIVE_CODE:
		fprintf(stderr, INVALID_ARCHIVE, prog, prog);
		goto any_error;
	case UNEXPECTED_EOF_CODE:
		fprintf(stderr, UNEXPECTED_EOF, prog, prog);
		goto any_error;
	case TL_IO_ERROR_CODE:
		fprintf(stderr, IO_ERROR, prog, file);
		/* fall through */
	default:
	any_error:
		exit_code = 2;
		goto close_input;
	}
	
	int unfound_file = 0;
	for (int i = 0; i < select_count; ++i) {
		if (*select_files[i]) {
			unfound_file = 1;
			fprintf(stderr, FILE_NOT_FOUND, prog, select_files[i]);
		}
	}
	if (unfound_file) {
		fprintf(stderr, NOT_FOUND_FINAL, prog);
		exit_code = 2;
	}

close_input:
	if (!fclose(fp))		
		return exit_code;
	fprintf(stderr, IO_ERROR, prog, file);
	return 2;
}
